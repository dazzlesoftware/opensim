"""Verify vendored bytes and reconstruct cpp-httplib upstream from local patches."""
import hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]/'third_party'
def reverse_patch(text,patch):
 lines=text.splitlines(keepends=True);diff=patch.splitlines(keepends=True);out=[];cursor=0;i=0
 while i<len(diff):
  match=re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@',diff[i])
  if not match:i+=1;continue
  start=int(match[3])-1;i+=1;before=[];after=[]
  while i<len(diff) and not diff[i].startswith('@@'):
   value=diff[i]
   if value.startswith((' ','+')):before.append(value[1:])
   if value.startswith((' ','-')):after.append(value[1:])
   i+=1
  assert lines[start:start+len(before)]==before,'Patch context mismatch'
  out+=lines[cursor:start]+after;cursor=start+len(before)
 return ''.join(out+lines[cursor:])
def main():
 manifest=json.loads((ROOT/'dependencies.json').read_text(encoding='utf-8'));checked=0
 for dependency in manifest['dependencies']:
  for item in dependency.get('files',[])+dependency.get('localPatches',[]):
   assert hashlib.sha256((ROOT/item['path']).read_bytes()).hexdigest()==item['sha256'],item['path'];checked+=1
 dependency=next(d for d in manifest['dependencies'] if d['name']=='yhirose/cpp-httplib');text=(ROOT/dependency['files'][0]['path']).read_text(encoding='utf-8')
 for patch in reversed(dependency['localPatches']):text=reverse_patch(text,(ROOT/patch['path']).read_text(encoding='utf-8'))
 assert hashlib.sha256(text.encode()).hexdigest()==dependency['files'][0]['upstreamSha256'],'Reconstructed upstream hash differs'
 print(f'PASS: {checked} vendored dependency hashes and exact cpp-httplib upstream reconstruction')
if __name__=='__main__':main()
