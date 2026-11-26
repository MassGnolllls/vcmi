"""vjson.load(path): VCMI's JSON (trailing commas, // comments)"""
import json, re
def loads(text):
    out=[]; i=0; n=len(text); in_str=False
    while i<n:
        c=text[i]
        if in_str:
            out.append(c)
            if c=='\\': out.append(text[i+1]); i+=1
            elif c=='"': in_str=False
        elif c=='"': in_str=True; out.append(c)
        elif c=='/' and text[i+1:i+2]=='/':
            while i<n and text[i]!='\n': i+=1
            continue
        else: out.append(c)
        i+=1
    t=''.join(out)
    t=re.sub(r',(\s*[}\]])', r'\1', t)
    return json.loads(t)
def load(path): return loads(open(path,encoding='utf-8').read())
