import {GRID} from './layout.js';
export const snapCoordinate=(value,step=GRID)=>Math.round(value/step)*step;
// Endpoints remain on the actual sockets; intermediate bends use world-grid coordinates.
export function orthogonalRoute(x1,y1,x2,y2,step=GRID){
  const exit=Math.ceil((x1+step)/step)*step,entry=Math.floor((x2-step)/step)*step;
  const a=snapCoordinate(y1,step),b=snapCoordinate(y2,step);
  if(entry>=exit){const middle=snapCoordinate((exit+entry)/2,step);return [[x1,y1],[exit,y1],[exit,a],[middle,a],[middle,b],[entry,b],[entry,y2],[x2,y2]]}
  // Backwards connections take a grid-aligned detour, preserving horizontal socket tangents.
  const detour=Math.floor((Math.min(y1,y2)-step*2)/step)*step;
  return [[x1,y1],[exit,y1],[exit,detour],[entry,detour],[entry,y2],[x2,y2]];
}
