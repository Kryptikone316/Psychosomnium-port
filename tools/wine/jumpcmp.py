import subprocess, os, sys, shutil
from PIL import Image
import numpy as np
W='../wine/frames_jump/'
def q(a): a=a.astype(int); return np.stack([a[...,0]>>3,a[...,1]>>2,a[...,2]>>3],-1)
def load(p): return q(np.array(Image.open(p).convert('RGB')))
def run(H, d):
    os.makedirs(d, exist_ok=True)
    env=dict(os.environ, PSY_HEADLESS='1', PSY_ROOM='1', PSY_SPAWN='1,224,150', PSY_JOY='120:4,%d:0'%(120+H), PSY_SHOTS=d, PSY_SHOT_FRAMES=','.join(str(n) for n in range(112,175)), PSY_MAXFRAMES='176')
    subprocess.run(['../../build-host/psychosomnium'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
mask=np.ones((480,640),bool); mask[215:240,500:530]=False
for So in (731,890,1021):
    best=None
    for H in range(2,50,1):
        d='jc/H%d'%H
        run(H,d)
        tot=0
        for k in range(-5,45):
            o=load(W+'f%04d.png'%(So+k)); m=load(d+'/frame%06d.bmp'%(121+k))
            tot+=int(((np.abs(o-m).sum(axis=2)>0)&mask).sum())
        if best is None or tot<best[0]: best=(tot,H)
        if tot==0: break
    print('jump starting at orig frame',So,': best hold H=%d frames (%.0f ms), residual diff pixels %d over 50 frames'%(best[1],best[1]/0.06,best[0]))
