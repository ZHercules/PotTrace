"""Fetch pinned Microsoft MIT-licensed sources into the ignored probe dependency folder."""
import concurrent.futures,hashlib,json,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
COMMIT='434f6002bdf9cf9829406c3ff2b33387982d6168'
REPOSITORY='microsoft/Windows-classic-samples'
SUBDIR='Samples/Win7Samples/multimedia/directshow/baseclasses'
dest=ROOT/'out/deps/directshow-baseclasses';dest.mkdir(parents=True,exist_ok=True)
def get(url):return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'PotTrace-isolated-probe'}),timeout=60).read()
items=json.loads(get(f'https://api.github.com/repos/{REPOSITORY}/contents/{SUBDIR}?ref={COMMIT}'))
def save(item):
 name=item['name']
 if Path(name).name!=name:raise ValueError('Unexpected dependency path')
 data=get(f'https://raw.githubusercontent.com/{REPOSITORY}/{COMMIT}/{SUBDIR}/{name}');(dest/name).write_bytes(data)
 return {'name':name,'sha256':hashlib.sha256(data).hexdigest()}
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:files=list(pool.map(save,[item for item in items if item['type']=='file']))
(dest/'LICENSE').write_bytes(get(f'https://raw.githubusercontent.com/{REPOSITORY}/{COMMIT}/LICENSE'))
(dest/'source-manifest.json').write_text(json.dumps({'commit':COMMIT,'repository':f'https://github.com/{REPOSITORY}','path':SUBDIR,'files':files},indent=2),encoding='utf-8')
print(f'{len(files)} pinned source files: {dest}')
