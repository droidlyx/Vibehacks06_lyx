"""Prepare the optional local AI runtime. No keys and no cloud inference charges."""
from pathlib import Path
import argparse, hashlib, json, shutil, urllib.request
ROOT=Path(__file__).resolve().parents[1]
LOCAL=ROOT/'Local'/'AI'
MODEL='Qwen3-1.7B-Q8_0.gguf'
REPO='Qwen/Qwen3-1.7B-GGUF'
def main():
    p=argparse.ArgumentParser();p.add_argument('--llama-source',type=Path,required=True);a=p.parse_args()
    LOCAL.mkdir(parents=True,exist_ok=True)
    metadata=json.load(urllib.request.urlopen('https://huggingface.co/api/models/'+REPO+'?blobs=true',timeout=30))
    entry=next(s for s in metadata['siblings'] if s['rfilename']==MODEL)
    sha=entry['lfs']['sha256'];size=entry['lfs']['size']
    target=LOCAL/MODEL
    def digest(path):
        h=hashlib.sha256()
        with path.open('rb') as f:
            for data in iter(lambda:f.read(8*1024*1024),b''):h.update(data)
        return h.hexdigest()
    if not target.exists() or target.stat().st_size!=size or digest(target)!=sha:
        part=target.with_suffix('.partial')
        with urllib.request.urlopen('https://huggingface.co/'+REPO+'/resolve/main/'+MODEL,timeout=60) as src,part.open('wb') as out:
            done=0;next_print=128*1024*1024
            while True:
                block=src.read(4*1024*1024)
                if not block:break
                out.write(block);done+=len(block)
                if done>=next_print:print(f'Model {done//1048576}/{size//1048576} MiB',flush=True);next_print+=128*1024*1024
        if part.stat().st_size!=size or digest(part)!=sha:raise RuntimeError('Downloaded model integrity check failed')
        part.replace(target)
    runtime=LOCAL/'llama';runtime.mkdir(exist_ok=True)
    if not (a.llama_source/'llama-server.exe').is_file():raise RuntimeError('llama-server.exe missing in supplied third-party runtime')
    for source in a.llama_source.iterdir():
        if source.is_file() and source.suffix.lower() in ('.dll','.exe'):
            shutil.copy2(source,runtime/source.name)
    urllib.request.urlretrieve('https://huggingface.co/'+REPO+'/resolve/main/LICENSE',LOCAL/'Qwen-LICENSE.txt')
    urllib.request.urlretrieve('https://raw.githubusercontent.com/ggml-org/llama.cpp/b11463/LICENSE',LOCAL/'llama-LICENSE.txt')
    (LOCAL/'provenance.json').write_text(json.dumps({'model':REPO,'model_file':MODEL,'sha256':sha,'size':size,'license':'Apache-2.0','runtime':'ggml-org/llama.cpp b11463','runtime_license':'MIT','note':'Vendor binaries only; no Ember game code or plugin is reused.'},indent=2),encoding='utf-8')
    print('Local AI runtime ready; weights excluded from Git.',flush=True)
if __name__=='__main__':main()
