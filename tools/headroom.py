"""Offline pre-serialization level admission. No hardware IO or automatic gain."""
import math

class HeadroomError(ValueError):
    def __init__(self,report):
        self.report=report
        super().__init__('render rejected at frame '+str(report['first_rejected_frame'])+
                         ': '+report['failure']+'; output not committed')

class Headroom:
    def __init__(self,ceiling_dbfs=None):
        if ceiling_dbfs is not None and (type(ceiling_dbfs) not in (int,float) or
                not -60<=ceiling_dbfs<=0 or not math.isfinite(ceiling_dbfs)):
            raise ValueError('ceiling dBFS must be finite in [-60,0]')
        self.ceiling_dbfs=ceiling_dbfs
        self.ceiling=None if ceiling_dbfs is None else 10.**(ceiling_dbfs/20.)
        self.frames=self.outside=self.clipped=self.rails=0;self.peak=0.
        self.first=None;self.failure=None;self.complete=False
    def observe(self,samples):
        if self.complete:raise ValueError('trace already finished')
        for y in samples:
            index=self.frames;self.frames+=1
            if not math.isfinite(y) or abs(y)>16:
                if self.first is None:self.first=index;self.failure='invalid float boundary'
                raise HeadroomError(self.report())
            a=abs(y);self.peak=max(self.peak,a);self.outside+=int(a>1.)
            q=round(y*32768);clipped=not -32768<=q<=32767
            self.clipped+=int(clipped);self.rails+=int(q<=-32768 or q>=32767)
            if self.ceiling is not None and self.first is None:
                if clipped:self.first=index;self.failure='PCM16 clipping'
                elif a>self.ceiling:self.first=index;self.failure='headroom ceiling exceeded'
        # Report the whole failing block; no later block or file is admitted.
        if self.first is not None:raise HeadroomError(self.report())
    def finish(self):
        if self.ceiling is not None and not self.frames:
            self.failure='empty trace';raise HeadroomError(self.report())
        self.complete=True
        return self.report()
    def report(self):
        return dict(scope='finite offline trace; not physical or all-input admission',
            ceiling_dbfs=self.ceiling_dbfs,ceiling_linear=self.ceiling,
            analyzed_frames=self.frames,float_peak=self.peak,
            outside_unity_frames=self.outside,pcm16_clipped_frames=self.clipped,
            pcm16_rail_frames=self.rails,first_rejected_frame=self.first,failure=self.failure,
            trace_complete=self.complete,
            admitted=False if self.failure else (True if self.complete and self.ceiling is not None else None))
