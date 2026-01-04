# buttonBox
buttonBox for simrace.

## PCB HARDWARE

Molex connectors are recommended, but any 2.54 mm pitch connector can be used as a substitute.

### BOM (Bill of Materials)
| Qty | Part | Description |
|-----|------|-------------|
| 1 | RP2040-Zero | MCU ([waveshare](https://www.waveshare.com/rp2040-zero.htm)) |
| 2 | MCP23017-E/SO | I2C IO expander ([mouser](https://www.mouser.com/ProductDetail/Microchip-Technology/MCP23017-E-SO?qs=usxtMOJb1Rz8hft7vV7YMQ%3D%3D)) |
| 5 | PEC11R-4315F-S0012 | Rotary Encoder (with button) ([mouser](https://www.mouser.com/ProductDetail/Bourns/PEC11R-4315F-S0012?qs=Zq5ylnUbLm4NmQU9A1GE4Q%3D%3D)) |
| 1 | SPST Rocker Switch with LED | 3-pin connector |
| 2 | 0603 4.7KΩ | I2C pull-up resistor |
| 6 | 0603 330Ω | LED resistor |
| 2 | 0603 0.1µF | Decoupling capacitor |
| 16 | Molex 5267-2 | 2-pin connector |
| 12 | Molex 5267-3 | 3-pin connector |

> See "Maximum Supported Inputs/Outputs" for compatible components.

### Maximum Supported Inputs/Outputs
| Type | Qty | Connector |
|------|-----|-----------|
| Rotary encoders (with button) | 5 | 3-pin |
| Self-return selectors | 5 | 3-pin |
| Momentary buttons | 6 | 2-pin |
| LED rocker switch | 2 | 3-pin |
| Keybox (ACC/START) | 1 | 2-pin |
| LED indicators | 6 | 2-pin |

## SimHub Integration

This button box supports [SimHub](https://www.simhubdash.com/) Custom Serial Device for LED feedback.

### Setup

1. Enable **Custom serial devices** plugin in SimHub Settings → Plugins
2. Add new serial device with these settings:

| Setting | Value |
|---------|-------|
| Serial port | (Select TinyUSB Serial) |
| Baudrate | 115200 |

### Update Messages

Add 3 messages with **"Use javascript"** enabled:

**Message 1 - RPM:**
```javascript
var rpmPercent = $prop('DataCorePlugin.GameData.NewData.CarSettings_CurrentGearRedLineRPM') > 0
    ? $prop('DataCorePlugin.GameData.NewData.Rpms') / $prop('DataCorePlugin.GameData.NewData.CarSettings_CurrentGearRedLineRPM') * 100
    : 0;

if (rpmPercent >= 95) return 'R';
else if (rpmPercent >= 85) return 'O';
else return 'r';
```

**Message 2 - TC:**
```javascript
if ($prop('DataCorePlugin.GameData.NewData.TCActive')) return 'T';
else return 't';
```

**Message 3 - ABS:**
```javascript
if ($prop('DataCorePlugin.GameData.NewData.ABSActive')) return 'A';
else return 'a';
```

### LED Protocol

| Command | LED | Action |
|---------|-----|--------|
| `R` | RPM | Blink (redzone) |
| `O` | RPM | On (optimal) |
| `r` | RPM | Off |
| `T` | TC | On |
| `t` | TC | Off |
| `A` | ABS | On |
| `a` | ABS | Off |


## ENCLOSURE

enclosure can attach to [Fanatec ClubSports Shifter SQ V1.5](https://www.fanatec.com/eu/en/p/add-ons/css_sq/clubsport-shifter-sq-v-1-5), using insert Nut
### requirement
- 4 x M5 nut
- 4 x M5 bolt
- 4 x M3 nut 
- 4 x M3 insert nut

## License
this Project uses multiple licenses : 

| Component | License |
|-----------|---------|
| Firmware (`/firmware`) | [GPL v3](firmware/LICENSE) |
| Hardware (`/hardware`) | [CC BY-SA 4.0](hardware/LICENSE) |
| Enclosure (`/enclosure`) | [CC BY 4.0](enclosure/LICENSE) |