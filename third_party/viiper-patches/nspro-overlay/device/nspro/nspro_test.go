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

func TestStickCalibrationWindows(t *testing.T) {
	dev, err := New(nil)
	if err != nil {
		t.Fatalf("New: %v", err)
	}

	block := dev.spiBlock(0x603D, 18)
	if len(block) != 18 {
		t.Fatalf("calibration length = %d", len(block))
	}

	leftMaxX, leftMaxY := unpackStick12(block[0:3])
	leftCenterX, leftCenterY := unpackStick12(block[3:6])
	leftMinX, leftMinY := unpackStick12(block[6:9])
	if leftMaxX != 2047 || leftMaxY != 2047 ||
		leftCenterX != StickCenter || leftCenterY != StickCenter ||
		leftMinX != 2048 || leftMinY != 2048 {
		t.Fatalf("left calibration max=%04x,%04x center=%04x,%04x min=%04x,%04x",
			leftMaxX, leftMaxY, leftCenterX, leftCenterY, leftMinX, leftMinY)
	}

	rightCenterX, rightCenterY := unpackStick12(block[9:12])
	rightMinX, rightMinY := unpackStick12(block[12:15])
	rightMaxX, rightMaxY := unpackStick12(block[15:18])
	if rightCenterX != StickCenter || rightCenterY != StickCenter ||
		rightMinX != 2048 || rightMinY != 2048 ||
		rightMaxX != 2047 || rightMaxY != 2047 {
		t.Fatalf("right calibration center=%04x,%04x min=%04x,%04x max=%04x,%04x",
			rightCenterX, rightCenterY, rightMinX, rightMinY, rightMaxX, rightMaxY)
	}
}

func TestIMUCalibrationWindow(t *testing.T) {
	dev, err := New(nil)
	if err != nil {
		t.Fatalf("New: %v", err)
	}

	block := dev.spiBlock(0x6020, 24)
	if len(block) != 24 {
		t.Fatalf("IMU calibration length = %d", len(block))
	}
	if unpackS16(block[0:2]) != 0 || unpackS16(block[2:4]) != 0 || unpackS16(block[4:6]) != 0 {
		t.Fatalf("unexpected accel origin: % x", block[0:6])
	}
	if unpackS16(block[6:8]) != 0x4000 ||
		unpackS16(block[8:10]) != 0x4000 ||
		unpackS16(block[10:12]) != 0x4000 {
		t.Fatalf("unexpected accel coeff: % x", block[6:12])
	}
	if unpackS16(block[12:14]) != 0 ||
		unpackS16(block[14:16]) != 0 ||
		unpackS16(block[16:18]) != 0 {
		t.Fatalf("unexpected gyro origin: % x", block[12:18])
	}
	if unpackS16(block[18:20]) != 0x343B ||
		unpackS16(block[20:22]) != 0x343B ||
		unpackS16(block[22:24]) != 0x343B {
		t.Fatalf("unexpected gyro coeff: % x", block[18:24])
	}
}

func unpackStick12(in []byte) (uint16, uint16) {
	x := uint16(in[0]) | ((uint16(in[1]) & 0x0F) << 8)
	y := (uint16(in[1]) >> 4) | (uint16(in[2]) << 4)
	return x, y
}

func unpackS16(in []byte) int16 {
	return int16(uint16(in[0]) | (uint16(in[1]) << 8))
}
