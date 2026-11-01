// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: CERN-OHL-S-2.0
/**
 * freedj control board: CUE / PLAY, their ring LEDs, and the pitch fader, behind one
 * I2C target (CH32V003F4P6) for the dev board. The AS5600 jog encoder shares the bus
 * through J_ENC (the CH32V003 doesn't touch it).
 *
 * Mechanical anchors (outline, screw holes, switch centres, LED circle, skirt ring,
 * connector positions) come from the enclosure CAD via gen_geom.py -> lib/geom.json.
 * Frame: board centre = (0, 0), +x right, +y toward the back of the deck, top side up
 * (toward the top plate). All SMD parts on top; the JST connectors on the bottom.
 *
 * CH32V003F4P6 pins (TSSOP-20, WCH pinout):
 *   1 PD4  LED_CUE1      11 PC1  SDA (I2C1 target)
 *   2 PD5  LED_CUE2      12 PC2  SCL
 *   3 PD6  LED_CUE3      13 PC3  INT (open drain, to host)
 *   4 PD7  NRST          14 PC4  POT (ADC A2)
 *   5 PA1  LED_PLAY1     15 PC5  BTN_PLAY (internal pull-up)
 *   6 PA2  LED_PLAY2     16 PC6  BTN_CUE  (internal pull-up)
 *   7 VSS                17 PC7  LED_CUE4
 *   8 PD0  LED_PLAY3     18 PD1  SWIO (programming)
 *   9 VDD                19 PD2  spare
 *  10 PC0  LED_PLAY4     20 PD3  spare
 */
import React from "react"
import geom from "./lib/geom.json"
import { CH32V003F4P6 } from "./imports/CH32V003F4P6"
import { ZX_QC66_7_5TP } from "./imports/ZX_QC66_7_5TP"
import { B3B_XH_A_LF__SN_ } from "./imports/B3B_XH_A_LF__SN_"
import { B5B_XH_A_LF__SN_ } from "./imports/B5B_XH_A_LF__SN_"

type XY = [number, number]
const rad = (d: number) => (d * Math.PI) / 180
const around = (c: XY, r: number, a: number): XY => [c[0] + r * Math.cos(rad(a)), c[1] + r * Math.sin(rad(a))]

// MCU: in the strip right of the buttons, pins 1-10 facing the buttons
const U1_AT: XY = [7.0, -3.0]
const COL = 12.6                      // passives column between U1 and the right-edge connectors
const LED_R = geom.ledRadius
const RES_R = 8.3                     // LED resistors, inside the skirt, on the LED's radius

// LCSC parts (JLCPCB basic where there is one)
const LCSC = {
  r100: "C22775", r470: "C23179", r1k: "C21190", r4k7: "C23162", r10k: "C25804",
  c100n: "C14663", c1u: "C28323", c10u: "C15850",
  ledGreen: "C2297", ledAmber: "C2296",
}

type Btn = { key: "play" | "cue"; label: string; led: "ledGreen" | "ledAmber"; res: "r100" | "r470"; pins: string[]; btnNet: string }
const BUTTONS: Btn[] = [
  { key: "play", label: "PLAY", led: "ledGreen", res: "r100", pins: ["pin5", "pin6", "pin8", "pin10"], btnNet: "net.BTN_PLAY" },
  { key: "cue", label: "CUE", led: "ledAmber", res: "r470", pins: ["pin1", "pin2", "pin3", "pin17"], btnNet: "net.BTN_CUE" },
]

const buttonBlock = (b: Btn) => {
  const c = geom.buttons[b.key] as XY
  const S = `SW_${b.label}`
  return (
    <>
      {/* diagonal pads: always across the switch, whatever its internal pairing */}
      <ZX_QC66_7_5TP name={S} pcbX={c[0]} pcbY={c[1]} />
      <trace from={`.${S} > .pin1`} to={b.btnNet} />
      <trace from={`.${S} > .pin4`} to="net.GND" />
      <silkscreentext text={b.label} pcbX={c[0]} pcbY={c[1] - 4.6} fontSize={1.0} />
      {geom.ledAngles.map((a, i) => {
        const [lx, ly] = around(c, LED_R, a)
        const [rx, ry] = around(c, RES_R, a)
        const D = `D_${b.label}${i + 1}`, R = `R_${b.label}${i + 1}`
        return (
          <React.Fragment key={D}>
            <led name={D} color={b.key === "play" ? "green" : "yellow"} footprint="0805"
                 pcbX={lx} pcbY={ly} pcbRotation={a + 90} supplierPartNumbers={{ jlcpcb: [LCSC[b.led]] }} />
            <resistor name={R} resistance={b.res === "r100" ? "100" : "470"} footprint="0603"
                      pcbX={rx} pcbY={ry} pcbRotation={a + 90} supplierPartNumbers={{ jlcpcb: [LCSC[b.res]] }} />
            <trace from={`.U1 > .${b.pins[i]}`} to={`.${R} > .pin1`} />
            <trace from={`.${R} > .pin2`} to={`.${D} > .anode`} />
            <trace from={`.${D} > .cathode`} to="net.GND" />
          </React.Fragment>
        )
      })}
    </>
  )
}

const J = geom.jst as Record<string, { pins: number; at: XY; along: string }>
const jRot = (n: string) => (J[n].along === "y" ? 90 : 0)

export default () => (
  <board width={`${geom.board.width}mm`} height={`${geom.board.height}mm`}
         borderRadius={`${geom.board.cornerRadius}mm`} routingDisabled>
    {/* screw holes onto the three bosses that come down from the top */}
    {geom.holes.map(([x, y], i) => (
      <hole key={i} name={`H${i + 1}`} diameter={`${geom.holeDiameter}mm`} pcbX={x} pcbY={y} />
    ))}

    <CH32V003F4P6 name="U1" pcbX={U1_AT[0]} pcbY={U1_AT[1]} pcbRotation={-90} />
    <trace from=".U1 > .VDD" to="net.V3V3" />
    <trace from=".U1 > .VSS" to="net.GND" />
    <trace from=".U1 > .pin11" to="net.SDA" />
    <trace from=".U1 > .pin12" to="net.SCL" />
    <trace from=".U1 > .pin13" to="net.INT" />
    <trace from=".U1 > .pin14" to="net.POT" />
    <trace from=".U1 > .pin15" to="net.BTN_PLAY" />
    <trace from=".U1 > .pin16" to="net.BTN_CUE" />
    <trace from=".U1 > .pin18" to="net.SWIO" />
    <trace from=".U1 > .pin4" to="net.NRST" />

    {/* decoupling: 100nF left of VDD (pin 9); 10uF bulk by J_HOST, where 3V3 comes in */}
    <capacitor name="C1" capacitance="100nF" footprint="0603" pcbX={U1_AT[0] - 5.0} pcbY={-5.3}
               pcbRotation={90} supplierPartNumbers={{ jlcpcb: [LCSC.c100n] }} />
    <trace from=".C1 > .pin1" to="net.V3V3" /><trace from=".C1 > .pin2" to="net.GND" />
    <capacitor name="C2" capacitance="10uF" footprint="0805" pcbX={-3.0} pcbY={21.2}
               pcbRotation={0} supplierPartNumbers={{ jlcpcb: [LCSC.c10u] }} />
    <trace from=".C2 > .pin1" to="net.V3V3" /><trace from=".C2 > .pin2" to="net.GND" />
    {/* NRST: pull-up + cap (the chip has one internally; this keeps it quiet on long wires) */}
    <resistor name="R_NRST" resistance="10k" footprint="0603" pcbX={U1_AT[0] - 5.0} pcbY={-0.6}
              pcbRotation={90} supplierPartNumbers={{ jlcpcb: [LCSC.r10k] }} />
    <trace from=".R_NRST > .pin1" to="net.V3V3" /><trace from=".R_NRST > .pin2" to="net.NRST" />

    {/* pitch fader wiper: RC low-pass into the ADC (1k + 100nF, ~1.6 kHz) */}
    <resistor name="R_POT" resistance="1k" footprint="0603" pcbX={COL} pcbY={-6.0} pcbRotation={90}
              supplierPartNumbers={{ jlcpcb: [LCSC.r1k] }} />
    <trace from=".R_POT > .pin1" to="net.WIPER" /><trace from=".R_POT > .pin2" to="net.POT" />
    <capacitor name="C_POT" capacitance="100nF" footprint="0603" pcbX={COL} pcbY={-9.2} pcbRotation={90}
               supplierPartNumbers={{ jlcpcb: [LCSC.c100n] }} />
    <trace from=".C_POT > .pin1" to="net.POT" /><trace from=".C_POT > .pin2" to="net.GND" />

    {/* I2C pull-ups (the P4's internal ones are weak) and the INT pull-up */}
    <resistor name="R_SDA" resistance="4.7k" footprint="0603" pcbX={COL} pcbY={0.6} pcbRotation={90}
              supplierPartNumbers={{ jlcpcb: [LCSC.r4k7] }} />
    <trace from=".R_SDA > .pin1" to="net.V3V3" /><trace from=".R_SDA > .pin2" to="net.SDA" />
    <resistor name="R_SCL" resistance="4.7k" footprint="0603" pcbX={COL} pcbY={3.6} pcbRotation={90}
              supplierPartNumbers={{ jlcpcb: [LCSC.r4k7] }} />
    <trace from=".R_SCL > .pin1" to="net.V3V3" /><trace from=".R_SCL > .pin2" to="net.SCL" />
    <resistor name="R_INT" resistance="10k" footprint="0603" pcbX={COL} pcbY={6.6} pcbRotation={90}
              supplierPartNumbers={{ jlcpcb: [LCSC.r10k] }} />
    <trace from=".R_INT > .pin1" to="net.V3V3" /><trace from=".R_INT > .pin2" to="net.INT" />

    {buttonBlock(BUTTONS[0])}
    {buttonBlock(BUTTONS[1])}

    {/* connectors, bottom side, plugs from below */}
    <B3B_XH_A_LF__SN_ name="J_POT" layer="bottom" pcbX={J.j_pot.at[0]} pcbY={J.j_pot.at[1]} pcbRotation={jRot("j_pot")} />
    <trace from=".J_POT > .pin1" to="net.V3V3" />
    <trace from=".J_POT > .pin2" to="net.WIPER" />
    <trace from=".J_POT > .pin3" to="net.GND" />
    <silkscreentext text="POT" layer="bottom" pcbX={J.j_pot.at[0] - 3.8} pcbY={J.j_pot.at[1]} fontSize={0.9} pcbRotation={90} />

    <B5B_XH_A_LF__SN_ name="J_ENC" layer="bottom" pcbX={J.j_enc.at[0]} pcbY={J.j_enc.at[1]} pcbRotation={jRot("j_enc")} />
    <trace from=".J_ENC > .pin1" to="net.V3V3" />
    <trace from=".J_ENC > .pin2" to="net.GND" />
    <trace from=".J_ENC > .pin3" to=".J_ENC > .pin2" />{/* AS5600 DIR: tied low, next to GND (the host can invert) */}
    <trace from=".J_ENC > .pin4" to="net.SCL" />
    <trace from=".J_ENC > .pin5" to="net.SDA" />
    <silkscreentext text="AS5600" layer="bottom" pcbX={J.j_enc.at[0] - 3.8} pcbY={J.j_enc.at[1]} fontSize={0.9} pcbRotation={90} />

    <B5B_XH_A_LF__SN_ name="J_HOST" layer="bottom" pcbX={J.j_host.at[0]} pcbY={J.j_host.at[1]} pcbRotation={jRot("j_host")} />
    <trace from=".J_HOST > .pin1" to="net.V3V3" />
    <trace from=".J_HOST > .pin2" to="net.GND" />
    <trace from=".J_HOST > .pin3" to="net.SDA" />
    <trace from=".J_HOST > .pin4" to="net.SCL" />
    <trace from=".J_HOST > .pin5" to="net.INT" />
    <silkscreentext text="HOST" layer="bottom" pcbX={J.j_host.at[0]} pcbY={J.j_host.at[1] - 3.8} fontSize={0.9} />

    {/* programming pads, bottom side, 2.54 mm apart: WCH-LinkE 3V3, GND, SWIO */}
    {([["TP_3V3", "net.V3V3"], ["TP_GND", "net.GND"], ["TP_SWIO", "net.SWIO"]] as const).map(([n, net], i) => (
      <React.Fragment key={n}>
        <testpoint name={n} footprintVariant="pad" padShape="circle" padDiameter="1.5mm" layer="bottom"
                   pcbX={-2.5 + i * 2.54} pcbY={-20.5} />
        <trace from={`.${n} > .pin1`} to={net} />
      </React.Fragment>
    ))}

    <silkscreentext text="freedj ctrl v1" layer="bottom" pcbX={2.0} pcbY={-14.0} fontSize={1.0} />
  </board>
)
