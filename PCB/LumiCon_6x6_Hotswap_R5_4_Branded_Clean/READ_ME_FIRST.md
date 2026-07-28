# Lumi-Con 6x6 Matrix Mini R5.3 Branded

This revision is electrically identical to the DRC-clean R5.2 board. Only silkscreen artwork and labels were added.

## Added to front silkscreen

- Simplified monochrome Lumi-Con matrix icon derived from `source_Lumi-Con.png`
- Monochrome `6X6 MATRIX MINI` wordmark derived from `source_6X6_MATRIX_MINI.png`
- `R5.3` revision mark
- J1 pin labels: `C0 R0 C1 R1 C2 R2 C3 R3 C4 R4 C5 R5`

## Added to back silkscreen

- Product/revision identification
- Mirrored J1 pin labels so the connector can be wired from either side
- J1 pin-1 note

## Mechanical

The four existing mounting holes remain unchanged:

- 3.2 mm NPTH clearance holes for M3 screws
- centres: (37.5,37.5), (162.5,37.5), (37.5,162.5), (162.5,162.5) mm in KiCad coordinates

## Before ordering

Open `LumiCon_6x6_Hotswap_R5_3_Branded.kicad_pro`, run KiCad DRC, and inspect both silkscreen layers in the 3D viewer/Gerber viewer. The artwork is confined to the top margin and does not alter copper, pads, drills, or board outline.
