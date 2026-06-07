package nspro

import (
	"encoding/binary"
	"io"
)

// viiper:wire nspro c2s buttons:u32 lx:u16 ly:u16 rx:u16 ry:u16 accelX:i16 accelY:i16 accelZ:i16 gyroX:i16 gyroY:i16 gyroZ:i16
type InputState struct {
	Buttons uint32

	LX, LY uint16
	RX, RY uint16

	AccelX, AccelY, AccelZ int16
	GyroX, GyroY, GyroZ    int16
}

func NewInputState() *InputState {
	return &InputState{
		LX: StickCenter,
		LY: StickCenter,
		RX: StickCenter,
		RY: StickCenter,
	}
}

func defaultInputState() *InputState { return NewInputState() }

type MetaState struct {
	SerialNumber  string `json:"serial_number"`
	BatteryLevel  uint8  `json:"battery_level"`
	Charging      bool   `json:"charging"`
	ExternalPower bool   `json:"external_power"`
}

func defaultMetaState() *MetaState {
	return &MetaState{
		SerialNumber:  DefaultSerial,
		BatteryLevel:  8,
		ExternalPower: true,
	}
}

func (s *InputState) MarshalBinary() ([]byte, error) {
	b := make([]byte, InputWireSize)
	binary.LittleEndian.PutUint32(b[0:4], s.Buttons)
	binary.LittleEndian.PutUint16(b[4:6], s.LX)
	binary.LittleEndian.PutUint16(b[6:8], s.LY)
	binary.LittleEndian.PutUint16(b[8:10], s.RX)
	binary.LittleEndian.PutUint16(b[10:12], s.RY)
	binary.LittleEndian.PutUint16(b[12:14], uint16(s.AccelX))
	binary.LittleEndian.PutUint16(b[14:16], uint16(s.AccelY))
	binary.LittleEndian.PutUint16(b[16:18], uint16(s.AccelZ))
	binary.LittleEndian.PutUint16(b[18:20], uint16(s.GyroX))
	binary.LittleEndian.PutUint16(b[20:22], uint16(s.GyroY))
	binary.LittleEndian.PutUint16(b[22:24], uint16(s.GyroZ))
	return b, nil
}

func (s *InputState) UnmarshalBinary(data []byte) error {
	if len(data) < InputWireSize {
		return io.ErrUnexpectedEOF
	}
	s.Buttons = binary.LittleEndian.Uint32(data[0:4])
	s.LX = binary.LittleEndian.Uint16(data[4:6])
	s.LY = binary.LittleEndian.Uint16(data[6:8])
	s.RX = binary.LittleEndian.Uint16(data[8:10])
	s.RY = binary.LittleEndian.Uint16(data[10:12])
	s.AccelX = int16(binary.LittleEndian.Uint16(data[12:14]))
	s.AccelY = int16(binary.LittleEndian.Uint16(data[14:16]))
	s.AccelZ = int16(binary.LittleEndian.Uint16(data[16:18]))
	s.GyroX = int16(binary.LittleEndian.Uint16(data[18:20]))
	s.GyroY = int16(binary.LittleEndian.Uint16(data[20:22]))
	s.GyroZ = int16(binary.LittleEndian.Uint16(data[22:24]))
	return nil
}

// viiper:wire nspro s2c leftRumble:u8*4 rightRumble:u8*4 flags:u8 playerLedMask:u8
type OutputState struct {
	LeftRumble    [4]byte
	RightRumble   [4]byte
	Flags         uint8
	PlayerLedMask uint8
}

func (o *OutputState) MarshalBinary() ([]byte, error) {
	b := make([]byte, OutputWireSize)
	copy(b[0:4], o.LeftRumble[:])
	copy(b[4:8], o.RightRumble[:])
	b[8] = o.Flags
	b[9] = o.PlayerLedMask
	return b, nil
}

func (o *OutputState) UnmarshalBinary(data []byte) error {
	if len(data) < OutputWireSize {
		return io.ErrUnexpectedEOF
	}
	copy(o.LeftRumble[:], data[0:4])
	copy(o.RightRumble[:], data[4:8])
	o.Flags = data[8]
	o.PlayerLedMask = data[9]
	return nil
}

func (s InputState) buildFullReport(counter uint8, meta MetaState, imuEnabled bool, vibrationOn bool) []byte {
	b := make([]byte, InputReportSize)
	b[0] = ReportIDInputFull
	b[1] = counter
	b[2] = powerInfo(meta, vibrationOn)

	buttons := s.buttonBytes()
	copy(b[3:6], buttons[:])
	packStick12(b[6:9], s.LX, s.LY)
	packStick12(b[9:12], s.RX, s.RY)
	b[12] = vibrationMarker(vibrationOn)

	if imuEnabled {
		for sample := 0; sample < 3; sample++ {
			off := 13 + sample*12
			binary.LittleEndian.PutUint16(b[off:off+2], uint16(s.AccelX))
			binary.LittleEndian.PutUint16(b[off+2:off+4], uint16(s.AccelY))
			binary.LittleEndian.PutUint16(b[off+4:off+6], uint16(s.AccelZ))
			binary.LittleEndian.PutUint16(b[off+6:off+8], uint16(s.GyroX))
			binary.LittleEndian.PutUint16(b[off+8:off+10], uint16(s.GyroY))
			binary.LittleEndian.PutUint16(b[off+10:off+12], uint16(s.GyroZ))
		}
	}

	return b
}

func (s InputState) buildSubcommandReply(counter uint8, meta MetaState, vibrationOn bool, subcmd uint8, payload []byte) []byte {
	b := s.buildFullReport(counter, meta, false, vibrationOn)
	b[0] = ReportIDSubcommandReply
	b[13] = 0x80
	b[14] = subcmd
	copy(b[15:], payload)
	return b
}

func (s InputState) buttonBytes() [3]byte {
	var out [3]byte
	encodeButtonMap(s.Buttons, buttonMap, out[:])
	return out
}

type buttonReportBit struct {
	button uint32
	index  int
	mask   byte
}

func encodeButtonMap(buttons uint32, mapping []buttonReportBit, out []byte) {
	for _, bit := range mapping {
		if buttons&bit.button != 0 {
			out[bit.index] |= bit.mask
		}
	}
}

var buttonMap = []buttonReportBit{
	{ButtonY, 0, 0x01},
	{ButtonX, 0, 0x02},
	{ButtonB, 0, 0x04},
	{ButtonA, 0, 0x08},
	{ButtonR, 0, 0x40},
	{ButtonZR, 0, 0x80},
	{ButtonMinus, 1, 0x01},
	{ButtonPlus, 1, 0x02},
	{ButtonRightStick, 1, 0x04},
	{ButtonLeftStick, 1, 0x08},
	{ButtonHome, 1, 0x10},
	{ButtonCapture, 1, 0x20},
	{ButtonDown, 2, 0x01},
	{ButtonUp, 2, 0x02},
	{ButtonRight, 2, 0x04},
	{ButtonLeft, 2, 0x08},
	{ButtonL, 2, 0x40},
	{ButtonZL, 2, 0x80},
}

func packStick12(out []byte, x, y uint16) {
	if len(out) < 3 {
		return
	}
	x = clampStick(x)
	y = clampStick(y)
	out[0] = byte(x)
	out[1] = byte((x>>8)&0x0F) | byte((y&0x0F)<<4)
	out[2] = byte(y >> 4)
}

func clampStick(v uint16) uint16 {
	if v > StickMax {
		return StickMax
	}
	return v
}

func powerInfo(meta MetaState, vibrationOn bool) uint8 {
	level := meta.BatteryLevel
	if level > 8 {
		level = 8
	}
	out := (level & 0x0F) << 4
	if meta.ExternalPower {
		out |= 0x01
	}
	if meta.Charging {
		out |= 0x02
	}
	if vibrationOn {
		out |= 0x04
	}
	return out
}

func vibrationMarker(vibrationOn bool) uint8 {
	if vibrationOn {
		return 0x38
	}
	return 0x30
}
