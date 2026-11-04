const fs=require('fs'), path=require('path'), esbuild=require('esbuild');
const frameworkDir=path.dirname(require.resolve('baklavajs'));
const resolveFramework=name=>require.resolve(name,{paths:[frameworkDir]});
const renderer=path.dirname(resolveFramework('@baklavajs/renderer-vue'));
// Small, version-pinned renderer seam: expose live connection state on the SVG path.
// No copy of the framework is maintained in this project.
const offlineConnections={name:'connection-state',setup(build){build.onLoad({filter:/renderer-vue\.es\.mjs$/},args=>{
  let contents=fs.readFileSync(args.path,'utf8');
  const original='state: _ctx.state\n  }, null, 8, ["x1", "y1", "x2", "y2", "state"])';
  if(!contents.includes(original))throw Error('Baklava connection renderer changed: review state seam');
  contents=contents.replace(original,'state: _ctx.state, class: {"offline": _ctx.connection.offline}\n  }, null, 8, ["x1", "y1", "x2", "y2", "state", "class"])');
  const routeAnchor='if (viewModel.value.settings.useStraightConnections) {';
  if(!contents.includes(routeAnchor))throw Error('Baklava routing renderer changed: review style seam');
  contents='import {orthogonalRoute} from '+JSON.stringify(path.join(__dirname,'src/routing.js').replace(/\\/g,'/'))+';\n'+contents;
  contents=contents.replace(routeAnchor,`if (viewModel.value.settings.connectionStyle === 'orthogonal') {
        return orthogonalRoute(props.x1, props.y1, props.x2, props.y2, viewModel.value.settings.wireGridSize || 24)
          .map(([x,y],i) => { const [tx,ty]=transform(x,y); return (i?'L ':'M ')+tx+' '+ty; }).join(' ');
      } else if (viewModel.value.settings.useStraightConnections) {`);
  const wheel='(...args) => unref(panZoom).onMouseWheel && unref(panZoom).onMouseWheel(...args),';
  if(!contents.includes(wheel))throw Error('Baklava wheel handler changed: review zoom guard');
  contents=contents.replace(wheel,'(...args) => args[0].preventDefault(),');
  const menuAnchor='return items;\n    });\n    const classes = computed';
  if(!contents.includes(menuAnchor))throw Error('Baklava protected node menu changed');
  contents=contents.replace(menuAnchor,'return props.node.protectedRoot ? items.filter(item => item.value !== "delete") : items;\n    });\n    const classes = computed');
  return {contents,loader:'js',resolveDir:renderer};
});}};
esbuild.build({entryPoints:[path.join(__dirname,'src/app.js')],bundle:true,minify:true,write:false,format:'iife',target:['chrome90','firefox90','safari15'],plugins:[offlineConnections],alias:{vue:require.resolve('vue/dist/vue.runtime.esm-bundler.js')},define:{__VUE_OPTIONS_API__:'true',__VUE_PROD_DEVTOOLS__:'false',__VUE_PROD_HYDRATION_MISMATCH_DETAILS__:'false'}}).then(result=>{
  const js=result.outputFiles[0].text.replace(/<\/script/gi,'<\\/script');
  const theme=fs.readFileSync(resolveFramework('@baklavajs/themes/dist/classic.css'),'utf8');
  const css=theme+'\n'+fs.readFileSync(path.join(__dirname,'src/style.css'),'utf8');
  const notices=fs.readFileSync(path.join(__dirname,'THIRD-PARTY-NOTICES.txt'),'utf8').replace(/-->/g,'-- >');
  const html='<!doctype html><!--\n'+notices+'\n--><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Powered Up Hub Control</title><style>'+css+'</style></head><body><div id="app"><p>Loading canvas…</p></div><script>'+js+'</script></body></html>';
  fs.writeFileSync(path.join(__dirname,'../main/web/index.html'),html,'utf8');
  console.log('Offline BaklavaJS panel: '+Buffer.byteLength(html)+' bytes');
}).catch(e=>{console.error(e);process.exit(1)});
