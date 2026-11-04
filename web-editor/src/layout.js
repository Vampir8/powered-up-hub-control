export const GRID=24;
// Ports share rows on both sides. Status rows span the full tile width.
export function layoutNode(node,el){
  const content=el.querySelector(':scope > .__content');if(!content)return;
  const inputs=Object.values(node.inputs).filter(i=>!i.hidden),outputs=Object.values(node.outputs).filter(i=>!i.hidden);
  const portInputs=inputs.filter(i=>i.port),portOutputs=outputs.filter(i=>i.port);
  const count=Math.max(portInputs.length,portOutputs.length),heights=Array(count).fill(GRID);
  for(const [list,column] of [[portInputs,1],[portOutputs,2]])list.forEach((intf,row)=>{const item=document.getElementById(intf.id);if(item){item.style.gridColumn=column;item.style.gridRow=row+1;item.style.height=GRID+'px'}});
  inputs.filter(i=>!i.port).forEach((intf,index)=>{const item=document.getElementById(intf.id);if(!item)return;item.style.gridColumn='1 / -1';item.style.gridRow=count+index+1;item.style.height='auto';const height=Math.max(GRID,Math.ceil(item.scrollHeight/GRID)*GRID);item.style.height=height+'px';heights.push(height)});
  content.style.gridTemplateRows=heights.map(h=>h+'px').join(' ');
  el.style.height=(GRID*2+heights.reduce((a,b)=>a+b,0))+'px';
}
