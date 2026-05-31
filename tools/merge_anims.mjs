// Merge Mixamo animation clips onto one base mesh.
// All inputs are the SAME character (identical bone names), each carrying one
// animation. We take the base (mesh + first clip), then graft each extra clip's
// channels onto the base by matching target nodes by NAME, deep-copying the
// sampler input/output accessors into the base document.
//
// Usage: node merge_anims.mjs base.glb out.glb name1=clip1.glb name2=clip2.glb ...
//   base.glb keeps its existing animation, renamed to baseName (arg "base=NAME").
import { NodeIO } from '@gltf-transform/core';

const args = process.argv.slice(2);
const basePath = args[0], outPath = args[1];
const baseName = (args.find(a=>a.startsWith('base=')) || 'base=walk').split('=')[1];
const clips = args.slice(2).filter(a=>a.includes('=') && !a.startsWith('base='))
  .map(a => { const i=a.indexOf('='); return { name:a.slice(0,i), path:a.slice(i+1) }; });

const io = new NodeIO();
const base = await io.read(basePath);
const broot = base.getRoot();

// name->node map in the base doc
const bnodes = new Map();
for (const n of broot.listNodes()) if (n.getName()) bnodes.set(n.getName(), n);

// rename base's existing animation
const baseAnims = broot.listAnimations();
if (baseAnims[0]) baseAnims[0].setName(baseName);
console.error(`base "${baseName}": ${baseAnims[0]?baseAnims[0].listChannels().length:0} channels`);

function copyAccessor(srcAcc) {
  // make a fresh accessor in the base doc with the same data
  const a = base.createAccessor()
    .setType(srcAcc.getType())
    .setArray(srcAcc.getArray().slice());
  return a;
}

for (const {name, path} of clips) {
  const src = await io.read(path);
  const sanim = src.getRoot().listAnimations()[0];
  if (!sanim) { console.error(`  ${name}: no animation, skipped`); continue; }
  const anim = base.createAnimation(name);
  let copied = 0, missing = 0;
  for (const ch of sanim.listChannels()) {
    const tnode = ch.getTargetNode();
    const tname = tnode ? tnode.getName() : null;
    const bnode = tname ? bnodes.get(tname) : null;
    if (!bnode) { missing++; continue; }
    const ssamp = ch.getSampler();
    const nsamp = base.createAnimationSampler()
      .setInterpolation(ssamp.getInterpolation())
      .setInput(copyAccessor(ssamp.getInput()))
      .setOutput(copyAccessor(ssamp.getOutput()));
    anim.addSampler(nsamp);
    const nch = base.createAnimationChannel()
      .setTargetNode(bnode).setTargetPath(ch.getTargetPath()).setSampler(nsamp);
    anim.addChannel(nch);
    copied++;
  }
  console.error(`  ${name}: ${copied} channels copied, ${missing} missing`);
}

await io.write(outPath, base);
console.error(`wrote ${outPath}`);
const names = broot.listAnimations().map(a=>a.getName());
console.error(`final clips: ${names.join(', ')}`);
