"""Inspect stable probe symbols; stack arguments are not classified as spills."""
from pathlib import Path
import hashlib,json,re,sys
root=Path(sys.argv[1]);records={}
for p in sorted(root.glob('*.s')):
 s=p.read_text();functions=[]
 for m in re.finditer(r'^(_stx_[A-Za-z0-9_]+):[^\n]*\n',s,re.M):
  end=s.find('.cfi_endproc',m.end())
  if end<0:continue
  body=s[m.end():end]
  functions.append(dict(symbol=m[1],instructions=len(re.findall(r'^\t[a-z][a-z0-9.]*\s',body,re.M)),conditional_branches=len(re.findall(r'^\t(?:b\.[a-z]+|cbz|cbnz|tbz|tbnz|j(?!mp)[a-z]+)\s',body,re.M)),calls=len(re.findall(r'^\t(?:callq|bl)\s',body,re.M)),tail_targets=re.findall(r'^\t(?:b|jmp)\s+(_[^\s,]+)',body,re.M),spill_annotations=body.count('Spill'),reload_annotations=body.count('Reload')))
 helpers=[]
 for target in sorted({target for f in functions for target in f['tail_targets']}):
  found=re.search(r'^'+re.escape(target)+r':[^\n]*\n',s,re.M)
  if found is None:continue
  end=s.find('.cfi_endproc',found.end())
  body=s[found.end():end]
  helpers.append(dict(symbol=target,instructions=len(re.findall(r'^\t[a-z][a-z0-9.]*\s',body,re.M)),conditional_branches=len(re.findall(r'^\t(?:b\.[a-z]+|cbz|cbnz|tbz|tbnz|j(?!mp)[a-z]+)\s',body,re.M)),spill_annotations=body.count('Spill'),calls=len(re.findall(r'^\t(?:callq|bl)\s',body,re.M))))
 records[p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'functions':functions,'tail_helpers':helpers}
(root/'audit.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records,indent=2))
