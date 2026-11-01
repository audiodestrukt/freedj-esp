import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["pin3"]
} as const

export const B3B_XH_A_LF__SN_ = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C144394"
  ]
}}
      manufacturerPartNumber="B3B_XH_A_LF__SN_"
      footprint={<footprint>
        <platedhole  portHints={["pin1"]} pcbX="-2.499995mm" pcbY="0mm" outerDiameter="1.5500096mm" holeDiameter="0.9000236mm" shape="circle" />
<platedhole  portHints={["pin2"]} pcbX="0.000127mm" pcbY="0mm" outerDiameter="1.5500096mm" holeDiameter="0.9000236mm" shape="circle" />
<platedhole  portHints={["pin3"]} pcbX="2.499995mm" pcbY="0mm" outerDiameter="1.5500096mm" holeDiameter="0.9000236mm" shape="circle" />
<silkscreenpath route={[{"x":4.599889200000121,"y":-0.19999960000006922},{"x":5.079288800000086,"y":-0.19999960000006922},{"x":5.079288800000086,"y":-0.15036800000007133}]} />
<silkscreenpath route={[{"x":4.599889200000121,"y":0.39997379999999794},{"x":5.079288800000086,"y":0.39997379999999794},{"x":5.079288800000086,"y":0.39570659999992586}]} />
<silkscreenpath route={[{"x":-4.966106400000058,"y":-0.23929340000006505},{"x":-4.500092599999903,"y":-0.23929340000006505},{"x":-4.500092599999903,"y":-0.19999960000006922}]} />
<silkscreenpath route={[{"x":-4.966106400000058,"y":0.3322065999999495},{"x":-4.500092599999903,"y":0.3322065999999495},{"x":-4.500092599999903,"y":0.3000247999999601}]} />
<silkscreenpath route={[{"x":-4.966106400000058,"y":-3.4035745999999563},{"x":4.96587779999993,"y":-3.4035745999999563}]} />
<silkscreenpath route={[{"x":-3.302152399999841,"y":1.93042539999999},{"x":-3.302152399999841,"y":2.4384254000000283}]} />
<silkscreenpath route={[{"x":-1.7781523999999536,"y":1.93042539999999},{"x":-1.7781523999999536,"y":2.4384254000000283}]} />
<silkscreenpath route={[{"x":3.3025334000001294,"y":1.93042539999999},{"x":3.3025334000001294,"y":2.209850799999913},{"x":3.3025334000001294,"y":2.4384254000000283}]} />
<silkscreenpath route={[{"x":1.7778476000000865,"y":1.93042539999999},{"x":1.7778476000000865,"y":2.4384254000000283}]} />
<silkscreenpath route={[{"x":4.96587779999993,"y":2.4384254000000283},{"x":-4.966106400000058,"y":2.4384254000000283}]} />
<silkscreenpath route={[{"x":5.079288800000086,"y":-3.4035745999999563},{"x":5.079288800000086,"y":2.4384254000000283}]} />
<silkscreenpath route={[{"x":-4.966106400000058,"y":2.4384254000000283},{"x":-4.966106400000058,"y":-3.4035745999999563}]} />
<silkscreentext text="{NAME}" pcbX="0.029083mm" pcbY="3.456434mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-5.275517000000036,"y":2.7064339999999447},{"x":5.333683000000065,"y":2.7064339999999447},{"x":5.333683000000065,"y":-3.6609660000000304},{"x":-5.275517000000036,"y":-3.6609660000000304},{"x":-5.275517000000036,"y":2.7064339999999447}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C144394.obj?uuid=7b9459f45f5f46e380bc2dba5f93c262",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C144394.step?uuid=7b9459f45f5f46e380bc2dba5f93c262",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0.00012700000002041634, y: 0.4689983000000666, z: -0.0000064000000006281255 },
      }}
      {...props}
    />
  )
}