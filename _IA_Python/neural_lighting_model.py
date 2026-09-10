"""Spatial RGB residual predictor for the paired direct-lighting experiment."""
import torch
from torch import nn
from torch.nn import functional as F


class LightingNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.full = nn.Sequential(nn.Conv2d(8,16,3,padding=1),nn.SiLU())
        self.encode_half = nn.Sequential(nn.Conv2d(16,24,3,stride=2,padding=1),nn.SiLU())
        self.quarter = nn.Sequential(nn.Conv2d(24,32,3,stride=2,padding=1),nn.SiLU(),
                                     nn.Conv2d(32,32,3,padding=1),nn.SiLU())
        self.decode_half = nn.Sequential(nn.Conv2d(56,24,3,padding=1),nn.SiLU())
        self.decode_full = nn.Sequential(nn.Conv2d(40,16,3,padding=1),nn.SiLU())
        self.output = nn.Conv2d(16,3,1)
        nn.init.zeros_(self.output.weight); nn.init.zeros_(self.output.bias)

    def forward(self, x):
        full = self.full(x)
        half = self.encode_half(full)
        low = self.quarter(half)
        mid = self.decode_half(torch.cat([F.interpolate(low,half.shape[-2:],mode='bilinear',align_corners=False),half],1))
        high = self.decode_full(torch.cat([F.interpolate(mid,full.shape[-2:],mode='bilinear',align_corners=False),full],1))
        return self.output(high)


class PointLightingNet(nn.Module):
    """Similar capacity, identical inputs, without neighboring pixels."""
    def __init__(self):
        super().__init__()
        self.layers = nn.Sequential(nn.Conv2d(8,191,1),nn.SiLU(),nn.Conv2d(191,191,1),nn.SiLU(),nn.Conv2d(191,3,1))
        nn.init.zeros_(self.layers[-1].weight); nn.init.zeros_(self.layers[-1].bias)

    def forward(self,x):
        return self.layers(x)


def predict_lighting(model,batch):
    # RGB residual can add/remove spatially varying colored light, unlike a
    # shared scalar gain. Bound is an experimental safeguard, not material ID.
    residual = .5*model(batch['features']).tanh()
    return torch.where(batch['valid']>0,(batch['source']+residual).clamp_min(0),batch['source'])


def lighting_loss(pred,batch):
    error = pred-batch['target']
    # Shadow region is a clean supervision mask, never an input to the network.
    weight = 1+2*(batch['shadow_effect']>.005)
    color = ((torch.sqrt(error.square()+1e-6)-.001)*weight).sum()/(3*weight.sum())
    gradient = (error[:,:,1:]-error[:,:,:-1]).abs().mean()+(error[:,:,:,1:]-error[:,:,:,:-1]).abs().mean()
    return color+.1*gradient
