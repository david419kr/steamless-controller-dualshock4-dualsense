package nspro

import "encoding/binary"

func (d *NSPro) handleUSBCommand(out []byte) {
	if len(out) < 2 {
		return
	}
	cmd := out[1]
	switch cmd {
	case usbCmdHandshake, usbCmdBaudrate3M, usbCmdNoTimeout, usbCmdEnableTO, usbCmdPreHandshake, usbCmdConnStatus:
		d.enqueueReport(usbResponse(cmd))
	case usbCmdReset:
		d.enqueueReport(usbResponse(cmd))
	default:
		d.enqueueReport(usbResponse(cmd))
	}
}

func (d *NSPro) handleRumbleOutput(out []byte) {
	if len(out) < 10 {
		return
	}
	var feedback OutputState
	copy(feedback.LeftRumble[:], out[2:6])
	copy(feedback.RightRumble[:], out[6:10])
	feedback.Flags = OutputFlagRumble

	d.protoMu.Lock()
	feedback.PlayerLedMask = d.playerLedMask
	d.protoMu.Unlock()

	d.emitOutput(feedback)
}

func (d *NSPro) handleSubcommand(out []byte) {
	if len(out) < 11 {
		return
	}

	subcmd := out[10]
	args := out[11:]
	payload := []byte{}
	ledMask := uint8(0)
	emitLed := false

	d.protoMu.Lock()
	switch subcmd {
	case subcmdRequestDeviceInfo:
		payload = []byte{
			0x03, 0x48, // firmware-ish version
			0x03,       // Pro Controller
			0x02,       // USB
			0x00, 0x1B, 0x7A, 0x01, 0x02, 0x03,
			0x02,
		}
	case subcmdSetReportMode:
		if len(args) >= 1 && args[0] == ReportIDInputFull {
			d.activeReportID = ReportIDInputFull
		}
	case subcmdSPIRead:
		payload = d.spiReadPayload(args)
	case subcmdSetPlayerLights:
		if len(args) >= 1 {
			d.playerLedMask = args[0]
			ledMask = args[0]
			emitLed = true
		}
		payload = []byte{}
	case subcmdEnableIMU:
		if len(args) >= 1 {
			d.imuEnabled = args[0] != 0
		}
	case subcmdSetIMUSensitivity:
		payload = []byte{}
	case subcmdEnableVibration:
		if len(args) >= 1 {
			d.vibrationOn = args[0] != 0
		}
	default:
		payload = []byte{}
	}
	d.protoMu.Unlock()

	d.stateMu.Lock()
	st := *d.inputState
	meta := *d.metaState
	d.stateMu.Unlock()

	d.protoMu.Lock()
	d.reportCounter++
	counter := d.reportCounter
	vibrationOn := d.vibrationOn
	d.protoMu.Unlock()

	d.enqueueReport(st.buildSubcommandReply(counter, meta, vibrationOn, subcmd, payload))

	if emitLed {
		var feedback OutputState
		feedback.Flags = OutputFlagLED
		feedback.PlayerLedMask = ledMask
		d.emitOutput(feedback)
	}
}

func (d *NSPro) spiReadPayload(args []byte) []byte {
	resp := make([]byte, 5)
	if len(args) < 5 {
		return resp
	}

	address := binary.LittleEndian.Uint32(args[0:4])
	length := int(args[4])
	if length < 0 {
		length = 0
	}
	if length > 0x30 {
		length = 0x30
	}

	resp = make([]byte, 5+length)
	binary.LittleEndian.PutUint32(resp[0:4], address)
	resp[4] = byte(length)
	copy(resp[5:], d.spiBlock(address, length))
	return resp
}

func (d *NSPro) spiBlock(address uint32, length int) []byte {
	block := make([]byte, length)
	switch {
	case address >= 0x603D && address < 0x6046:
		fillCalibrationWindow(block, address, 0x603D, true)
	case address >= 0x6046 && address < 0x604F:
		fillCalibrationWindow(block, address, 0x6046, false)
	case address >= 0x6020 && address < 0x6038:
		fillIMUCalibrationWindow(block, address, 0x6020)
	case address >= 0x6050 && address < 0x6060:
		serial := []byte(d.metaSerialNumber())
		copyWindow(block, address, 0x6050, serial)
	case address >= 0x8000 && address < 0x8050:
		// User calibration is intentionally left empty so hosts use factory data.
	default:
	}
	return block
}

func fillCalibrationWindow(out []byte, address uint32, base uint32, left bool) {
	cal := make([]byte, 9)
	if left {
		encodeStickCalibration(cal, StickCenter, StickCenter, 0x0700, 0x0700, 0x0700, 0x0700)
	} else {
		encodeStickCalibration(cal, StickCenter, StickCenter, 0x0700, 0x0700, 0x0700, 0x0700)
	}
	copyWindow(out, address, base, cal)
}

func fillIMUCalibrationWindow(out []byte, address uint32, base uint32) {
	cal := make([]byte, 24)
	putS16(cal[0:2], 0)
	putS16(cal[2:4], 0)
	putS16(cal[4:6], 0)
	putS16(cal[6:8], 4096)
	putS16(cal[8:10], 4096)
	putS16(cal[10:12], 4096)
	putS16(cal[12:14], 0)
	putS16(cal[14:16], 0)
	putS16(cal[16:18], 0)
	putS16(cal[18:20], 14)
	putS16(cal[20:22], 14)
	putS16(cal[22:24], 14)
	copyWindow(out, address, base, cal)
}

func encodeStickCalibration(out []byte, neutralX, neutralY, maxX, maxY, minX, minY uint16) {
	if len(out) < 9 {
		return
	}
	packStick12(out[0:3], neutralX, neutralY)
	packStick12(out[3:6], maxX, maxY)
	packStick12(out[6:9], minX, minY)
}

func copyWindow(out []byte, address uint32, base uint32, src []byte) {
	if address < base {
		return
	}
	offset := int(address - base)
	if offset >= len(src) {
		return
	}
	copy(out, src[offset:])
}

func putS16(out []byte, value int16) {
	binary.LittleEndian.PutUint16(out, uint16(value))
}

func usbResponse(cmd uint8) []byte {
	resp := make([]byte, InputReportSize)
	resp[0] = ReportIDUSBResponse
	resp[1] = cmd
	resp[2] = 0x00
	resp[3] = 0x03
	return resp
}
