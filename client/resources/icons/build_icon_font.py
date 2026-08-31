#!/usr/bin/env python3
"""Regenera templar-icons.ttf a partir de Noto Sans Symbols2 (OFL-1.1),
subseteado a solo los codepoints listados abajo. Manten este script y la
fuente origen en sincronia con client/resources/icons/NOTICE.

Para anadir un icono nuevo: agrega su codepoint a ICON_CODEPOINTS, vuelve
a ejecutar este script, y copia el .ttf resultante a
client/resources/templar-icons.ttf Y phone/qml/assets/templar-icons.ttf
(mismo criterio que great_helmet_logo.png: cada modulo QML gestiona su
propia copia, ver el comentario en phone/CMakeLists.txt).

Uso:
  pip install fonttools
  curl -L -o NotoSansSymbols2-Regular.ttf \
    https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssymbols2/NotoSansSymbols2-Regular.ttf
  python3 build_icon_font.py
"""
from fontTools import subset
from fontTools.ttLib import TTFont

SOURCE_FONT = "NotoSansSymbols2-Regular.ttf"
OUTPUT_FONT = "templar-icons.ttf"
FAMILY_NAME = "Templar Icons"

# U+27A4 BLACK RIGHTWARDS ARROWHEAD -- icono de "enviar".
ICON_CODEPOINTS = [0x27A4]


def main():
    font = TTFont(SOURCE_FONT)

    subsetter = subset.Subsetter()
    subsetter.populate(unicodes=ICON_CODEPOINTS)
    subsetter.subset(font)

    # Renombra la familia para que no se confunda con una "Noto Sans
    # Symbols2" del sistema (que tendria metricas/cobertura distintas) --
    # este .ttf solo sirve para los codepoints de arriba, nada mas.
    name_table = font["name"]
    for name_id in (1, 4, 6, 16):
        name_table.setName(FAMILY_NAME, name_id, 3, 1, 0x409)
        name_table.setName(FAMILY_NAME, name_id, 1, 0, 0)

    font.save(OUTPUT_FONT)
    print(f"Escrito {OUTPUT_FONT} con {len(ICON_CODEPOINTS)} icono(s)")


if __name__ == "__main__":
    main()
