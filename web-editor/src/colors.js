export const LED_COLORS=[
  ['Red',0xff0000],['Orange',0xff6000],['Amber',0xffbf00],['Yellow',0xffff00],
  ['Lime',0xbfff00],['Light green',0x60ff00],['Green',0x00ff00],['Emerald',0x00bf60],
  ['Mint',0x60ffbf],['Teal',0x00bfbf],['Cyan',0x00ffff],['Sky blue',0x00bfff],
  ['Blue',0x0000ff],['Cobalt',0x0040bf],['Indigo',0x4000bf],['Violet',0x8000ff],
  ['Lavender',0xbf80ff],['Purple',0xbf00ff],['Magenta',0xff00ff],['Pink',0xff0080],
  ['Light pink',0xff80bf],['Warm white',0xffdfaf],['White',0xffffff],['Off',0x000000]
];
export const colorHex=rgb=>'#'+rgb.toString(16).padStart(6,'0');
export const colorName=rgb=>LED_COLORS.find(([,value])=>value===rgb)?.[0]||(rgb>=0?'Custom color':'Automatic color');
