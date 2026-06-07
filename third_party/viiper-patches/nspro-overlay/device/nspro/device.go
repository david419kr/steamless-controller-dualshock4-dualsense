// Package nspro provides a Nintendo Switch Pro Controller compatible HID device.
package nspro

import (
	"encoding/json"
	"fmt"
	"sync"

	"github.com/Alia5/VIIPER/device"
	"github.com/Alia5/VIIPER/usb"
	"github.com/Alia5/VIIPER/usbip"
)

type NSPro struct {
	stateMu        sync.Mutex
	inputState     *InputState
	metaState      *MetaState
	outputMu       sync.RWMutex
	outputCallback func(OutputState)
	outputVersion  uint64
	descriptor     usb.Descriptor

	protoMu        sync.Mutex
	activeReportID uint8
	imuEnabled     bool
	vibrationOn    bool
	playerLedMask  uint8
	reportCounter  uint8
	pendingReports [][]byte
}

func New(o *device.CreateOptions) (*NSPro, error) {
	metaState := defaultMetaState()
	if o != nil && o.DeviceSpecific != nil {
		var newMeta MetaState
		data, err := json.Marshal(o.DeviceSpecific)
		if err != nil {
			return nil, fmt.Errorf("marshal device specific args: %w", err)
		}
		if err := json.Unmarshal(data, &newMeta); err != nil {
			return nil, fmt.Errorf("invalid device specific JSON: %w", err)
		}
		if newMeta.SerialNumber != "" {
			metaState.SerialNumber = newMeta.SerialNumber
		}
		if newMeta.BatteryLevel != 0 {
			metaState.BatteryLevel = newMeta.BatteryLevel
		}
		if newMeta.Charging {
			metaState.Charging = true
		}
		if newMeta.ExternalPower {
			metaState.ExternalPower = true
		}
	}

	d := &NSPro{
		inputState:     defaultInputState(),
		metaState:      metaState,
		descriptor:     MakeDescriptor(),
		activeReportID: ReportIDInputFull,
		imuEnabled:     true,
		vibrationOn:    true,
	}
	if metaState.SerialNumber != "" {
		d.descriptor.Strings[3] = metaState.SerialNumber
	}

	if o != nil {
		if o.IdVendor != nil {
			d.descriptor.Device.IDVendor = *o.IdVendor
		}
		if o.IdProduct != nil {
			d.descriptor.Device.IDProduct = *o.IdProduct
		}
	}
	return d, nil
}

func (d *NSPro) SetOutputCallback(f func(OutputState)) func() {
	d.outputMu.Lock()
	d.outputVersion++
	version := d.outputVersion
	d.outputCallback = f
	d.outputMu.Unlock()

	return func() {
		d.outputMu.Lock()
		if d.outputVersion == version {
			d.outputCallback = nil
		}
		d.outputMu.Unlock()
	}
}

func (d *NSPro) UpdateInputState(state InputState) {
	d.stateMu.Lock()
	defer d.stateMu.Unlock()
	d.inputState = &state
}

func (d *NSPro) SetMetaState(meta MetaState) {
	d.stateMu.Lock()
	defer d.stateMu.Unlock()
	d.metaState = &meta
	if d.descriptor.Strings != nil && meta.SerialNumber != "" {
		d.descriptor.Strings[3] = meta.SerialNumber
	}
}

func (d *NSPro) HandleTransfer(ep uint32, dir uint32, out []byte) []byte {
	switch {
	case dir == usbip.DirIn && ep == 1:
		return d.nextInputReport()
	case dir == usbip.DirOut && ep == 1:
		d.handleOutputReport(out)
	}
	return nil
}

func (d *NSPro) HandleControl(bmRequestType, bRequest uint8, wValue, wIndex uint16, wLength uint16, data []byte) ([]byte, bool) {
	const (
		hidGetReport = 0x01
		hidSetReport = 0x09
	)
	const (
		reportTypeInput  = 0x01
		reportTypeOutput = 0x02
	)

	reportType := uint8(wValue >> 8)
	reportID := uint8(wValue)

	if bmRequestType == 0xA1 && bRequest == hidGetReport && reportType == reportTypeInput {
		switch reportID {
		case ReportIDInputFull, ReportIDSubcommandReply, ReportIDUSBResponse, 0:
			return d.inputReportForID(reportID), true
		}
	}

	if bmRequestType == 0x21 && bRequest == hidSetReport && reportType == reportTypeOutput {
		d.handleOutputReport(append([]byte{reportID}, data...))
		return nil, true
	}

	return nil, false
}

func (d *NSPro) GetDescriptor() *usb.Descriptor {
	return &d.descriptor
}

func (d *NSPro) GetDeviceSpecificArgs() map[string]any {
	d.stateMu.Lock()
	defer d.stateMu.Unlock()
	if d.metaState == nil {
		return map[string]any{}
	}
	b, err := json.Marshal(d.metaState)
	if err != nil {
		return map[string]any{}
	}
	var out map[string]any
	if err := json.Unmarshal(b, &out); err != nil {
		return map[string]any{}
	}
	return out
}

func (d *NSPro) nextInputReport() []byte {
	d.protoMu.Lock()
	if len(d.pendingReports) > 0 {
		report := d.pendingReports[0]
		d.pendingReports = d.pendingReports[1:]
		d.protoMu.Unlock()
		return append([]byte(nil), report...)
	}
	reportID := d.activeReportID
	d.protoMu.Unlock()
	return d.inputReportForID(reportID)
}

func (d *NSPro) inputReportForID(reportID uint8) []byte {
	d.stateMu.Lock()
	st := *d.inputState
	meta := *d.metaState
	d.stateMu.Unlock()

	d.protoMu.Lock()
	if reportID == 0 {
		reportID = d.activeReportID
	}
	d.reportCounter++
	counter := d.reportCounter
	imuEnabled := d.imuEnabled
	vibrationOn := d.vibrationOn
	d.protoMu.Unlock()

	switch reportID {
	case ReportIDUSBResponse:
		return usbResponse(usbCmdHandshake)
	case ReportIDSubcommandReply:
		return st.buildSubcommandReply(counter, meta, vibrationOn, subcmdRequestDeviceInfo, []byte{0x03, 0x48, 0x03, 0x02})
	default:
		return st.buildFullReport(counter, meta, imuEnabled, vibrationOn)
	}
}

func (d *NSPro) handleOutputReport(out []byte) {
	if len(out) == 0 {
		return
	}

	switch out[0] {
	case ReportIDOutputUSB:
		d.handleUSBCommand(out)
	case ReportIDOutputSubcmd:
		d.handleRumbleOutput(out)
		d.handleSubcommand(out)
	case ReportIDOutputRumble:
		d.handleRumbleOutput(out)
	default:
	}
}

func (d *NSPro) emitOutput(feedback OutputState) {
	d.outputMu.RLock()
	callback := d.outputCallback
	d.outputMu.RUnlock()
	if callback != nil {
		callback(feedback)
	}
}

func (d *NSPro) enqueueReport(report []byte) {
	d.protoMu.Lock()
	defer d.protoMu.Unlock()
	d.pendingReports = append(d.pendingReports, append([]byte(nil), report...))
}
