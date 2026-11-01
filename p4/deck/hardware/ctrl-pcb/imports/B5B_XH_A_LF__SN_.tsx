import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["pin3"],
  pin4: ["pin4"],
  pin5: ["pin5"]
} as const

export const B5B_XH_A_LF__SN_ = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C157991"
  ]
}}
      manufacturerPartNumber="B5B_XH_A_LF__SN_"
      footprint={<footprint>
        <platedhole  portHints={["pin1"]} pcbX="5.08mm" pcbY="0mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin2"]} pcbX="2.54mm" pcbY="0mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin3"]} pcbX="0mm" pcbY="0mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin4"]} pcbX="-2.54mm" pcbY="0mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin5"]} pcbX="-5.08mm" pcbY="0mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<silkscreenpath route={[{"x":-7.409967400000028,"y":-2.3500080000000025},{"x":7.409992800000055,"y":-2.3500080000000025}]} />
<silkscreenpath route={[{"x":7.409967399999914,"y":-2.3500080000000025},{"x":7.409992800000055,"y":3.399967800000013}]} />
<silkscreenpath route={[{"x":7.409992800000055,"y":3.399967800000013},{"x":-7.409967400000028,"y":3.399967800000013}]} />
<silkscreenpath route={[{"x":-7.409967400000028,"y":3.39999320000004},{"x":-7.409967400000028,"y":-2.3500080000000025}]} />
<silkscreenpath route={[{"x":-5.33400000000006,"y":-2.286000000000058},{"x":-5.33400000000006,"y":-1.7779999999999063},{"x":5.333999999999946,"y":-1.7779999999999063},{"x":5.333999999999946,"y":-2.286000000000058},{"x":5.333999999999946,"y":-2.286000000000058}]} />
<silkscreentext text="{NAME}" pcbX="-0.0127mm" pcbY="4.5052mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-7.768400000000042,"y":3.7552000000001726},{"x":7.742999999999938,"y":3.7552000000001726},{"x":7.742999999999938,"y":-2.6884000000000015},{"x":-7.768400000000042,"y":-2.6884000000000015},{"x":-7.768400000000042,"y":3.7552000000001726}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C157991.obj?uuid=9711208b2b1f45589f7edb7a0439d8bc",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C157991.step?uuid=9711208b2b1f45589f7edb7a0439d8bc",
        pcbRotationOffset: 180,
        modelOriginPosition: { x: 4.999009399999977, y: 0, z: 0.49999300000000035 },
      }}
      {...props}
    />
  )
}