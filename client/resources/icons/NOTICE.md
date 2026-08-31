`templar-icons.ttf` (en `client/resources/` y `phone/qml/assets/`, mismo
archivo copiado en ambos sitios) es un subconjunto de **Noto Sans
Symbols2**, generado con `build_icon_font.py` de este directorio.

- Fuente original: https://fonts.google.com/noto/specimen/Noto+Sans+Symbols+2
- Licencia: SIL Open Font License 1.1 (`OFL.txt`, copiada aqui tal cual la
  distribuye Google Fonts) -- permite subsetear/renombrar/redistribuir
  libremente junto con la app, con la unica condicion de no vender la
  fuente por si sola.
- Por que un subset: la fuente completa pesa ~1.2 MB; solo necesitamos un
  puñado de glifos concretos (iconos de la UI) que no estan garantizados
  en las fuentes que trae cada sistema/movil, asi que se empaqueta un
  recorte de unos pocos KB en vez de la fuente entera o de depender de que
  el dispositivo la tenga instalada.

Para anadir un icono nuevo: edita `ICON_CODEPOINTS` en
`build_icon_font.py`, vuelve a ejecutarlo, y copia el `.ttf` resultante a
las dos ubicaciones de arriba.
