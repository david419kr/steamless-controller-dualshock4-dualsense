package nspro

const (
	DefaultVID          = 0x057E
	DefaultPID          = 0x2009
	DefaultSerialEnding = "0001"
	DefaultSerial       = "000000000001"
)

const (
	EndpointHIDIn  = 0x81
	EndpointHIDOut = 0x01
)

const (
	ReportIDInputFull       = 0x30
	ReportIDSubcommandReply = 0x21
	ReportIDUSBResponse     = 0x81
	ReportIDOutputSubcmd    = 0x01
	ReportIDOutputRumble    = 0x10
	ReportIDOutputUSB       = 0x80
)

const (
	InputReportSize  = 64
	OutputReportSize = 64
	InputWireSize    = 24
	OutputWireSize   = 10
)

const (
	OutputFlagRumble = 0x01
	OutputFlagLED    = 0x02
)

const (
	StickCenter uint16 = 0x0800
	StickMax    uint16 = 0x0FFF
)

const (
	ButtonB uint32 = 1 << iota
	ButtonA
	ButtonY
	ButtonX
	ButtonR
	ButtonZR
	ButtonPlus
	ButtonRightStick
	ButtonDown
	ButtonRight
	ButtonLeft
	ButtonUp
	ButtonL
	ButtonZL
	ButtonMinus
	ButtonLeftStick
	ButtonHome
	ButtonCapture
)

const (
	subcmdRequestDeviceInfo = 0x02
	subcmdSetReportMode     = 0x03
	subcmdSPIRead           = 0x10
	subcmdSetPlayerLights   = 0x30
	subcmdEnableIMU         = 0x40
	subcmdSetIMUSensitivity = 0x41
	subcmdEnableVibration   = 0x48
)

const (
	usbCmdConnStatus  = 0x01
	usbCmdHandshake   = 0x02
	usbCmdBaudrate3M  = 0x03
	usbCmdNoTimeout   = 0x04
	usbCmdEnableTO    = 0x05
	usbCmdReset       = 0x06
	usbCmdPreHandshake = 0x91
)
