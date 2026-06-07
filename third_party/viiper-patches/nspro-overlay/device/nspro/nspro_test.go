package nspro

import (
	"bytes"
	"testing"

	"github.com/Alia5/VIIPER/usbip"
)

func TestDescriptorIdentityAndReport(t *testing.T) {
	desc := MakeDescriptor()

	if desc.Device.IDVendor != DefaultVID || desc.Device.IDProduct != DefaultPID {
		t.Fatalf("unexpected VID/PID: %04x:%04x", desc.Device.IDVendor, desc.Device.IDProduct)
	}
	if desc.Strings[2] != "Pro Controller" {
		t.Fatalf("unexpected product string: %q", desc.Strings[2])
	}
	if len(desc.Interfaces) != 1 {
		t.Fatalf("expected one HID interface, got %d", len(desc.Interfaces))
	}

	iface := desc.Interfaces[0]
	if iface.HID == nil {
		t.Fatalf("missing HID function")
	}
	if got := iface.HID.Descriptor.Descriptors[0].Length; got != 203 {
		t.Fatalf("unexpected HID descriptor length field: %d", got)
	}
	report, err := iface.HID.Report.Bytes()
	if err != nil {
		t.Fatalf("encode report descriptor: %v", err)
	}
	if len(report) != 203 {
		t.Fatalf("unexpected report descriptor length: %d", len(report))
	}
	if len(iface.Endpoints) != 2 ||
		iface.Endpoints[0].BEndpointAddress != EndpointHIDIn ||
		iface.Endpoints[1].BEndpointAddress != EndpointHIDOut {
		t.Fatalf("unexpected endpoints: %#v", iface.Endpoints)
	}
}

func TestFullInputReport(t *testing.T) {
	dev, err := New(nil)
	if err != nil {
		t.Fatalf("New: %v", err)
	}

	state := *NewInputState()
	state.Buttons = ButtonA | ButtonZL | ButtonCapture
	state.LX = 0x0123
	state.LY = 0x0456
	state.RX = 0x0789
	state.RY = 0x0ABC
	state.AccelX = 11
	state.AccelY = -22
	state.AccelZ = 33
	state.GyroX = -44
	state.GyroY = 55
	state.GyroZ = -66
	dev.UpdateInputState(state)

	report := dev.HandleTransfer(1, usbip.DirIn, nil)
	if len(report) != InputReportSize {
		t.Fatalf("unexpected input report length: %d", len(report))
	}
	if report[0] != ReportIDInputFull {
		t.Fatalf("unexpected report id: %02x", report[0])
	}

	buttons := state.buttonBytes()
	if !bytes.Equal(report[3:6], buttons[:]) {
		t.Fatalf("button bytes mismatch: got % x want % x", report[3:6], buttons[:])
	}
	packedLeft := make([]byte, 3)
	packedRight := make([]byte, 3)
	packStick12(packedLeft, state.LX, state.LY)
	packStick12(packedRight, state.RX, state.RY)
	if !bytes.Equal(report[6:9], packedLeft) || !bytes.Equal(report[9:12], packedRight) {
		t.Fatalf("stick packing mismatch: left=% x/% x right=% x/% x", report[6:9], packedLeft, report[9:12], packedRight)
	}
}

func TestRumbleAndSubcommandOutput(t *testing.T) {
	dev, err := New(nil)
	if err != nil {
		t.Fatalf("New: %v", err)
	}

	var got []OutputState
	dev.SetOutputCallback(func(out OutputState) {
		got = append(got, out)
	})

	packet := make([]byte, OutputReportSize)
	packet[0] = ReportIDOutputSubcmd
	copy(packet[2:6], []byte{0x01, 0x02, 0x03, 0x04})
	copy(packet[6:10], []byte{0x05, 0x06, 0x07, 0x08})
	packet[10] = subcmdSetPlayerLights
	packet[11] = 0x0B
	dev.HandleTransfer(1, usbip.DirOut, packet)

	if len(got) != 2 {
		t.Fatalf("expected rumble and LED callbacks, got %d", len(got))
	}
	if got[0].Flags != OutputFlagRumble ||
		!bytes.Equal(got[0].LeftRumble[:], []byte{0x01, 0x02, 0x03, 0x04}) ||
		!bytes.Equal(got[0].RightRumble[:], []byte{0x05, 0x06, 0x07, 0x08}) {
		t.Fatalf("unexpected rumble callback: %#v", got[0])
	}
	if got[1].Flags != OutputFlagLED || got[1].PlayerLedMask != 0x0B {
		t.Fatalf("unexpected LED callback: %#v", got[1])
	}

	reply := dev.HandleTransfer(1, usbip.DirIn, nil)
	if len(reply) != InputReportSize || reply[0] != ReportIDSubcommandReply || reply[14] != subcmdSetPlayerLights {
		t.Fatalf("unexpected subcommand reply: % x", reply[:16])
	}
}
