"""Known-camera ray-traced fixtures for the trained relighting DX12 pass.

Uses analytic spheres, positive linear view Z and fixed pinhole projection.
Normals are oriented to +Z consistently in both oracle and reconstruction.
No ray tracing API, pretrained third-party model or game capture is used.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from train_relighting_fixture import apply, illumination, normalize


def render(width, height, offset=0.):
    y, x = np.mgrid[:height, :width].astype(np.float32)
    fx = fy = np.float32(height * 1.2)
    cx, cy = (width-1)/2, (height-1)/2
    rays = np.stack([(x-cx)/fx, (y-cy)/fy, np.ones_like(x)], -1)
    z = np.full_like(x, 6)
    normal = np.zeros_like(rays); normal[..., 2] = 1
    material = np.full_like(rays, [.29, .33, .38])
    # New geometry/materials relative to the training generator.
    objects = [(-1.35+offset, -.18, 4.4, .95, [.68, .21, .10]),
               (.15+offset, .45, 3.8, .72, [.12, .45, .58]),
               (1.25+offset, -.25, 4.7, .9, [.56, .46, .17])]
    aa = np.sum(rays*rays, -1)
    for sx, sy, sz, radius, rgb in objects:
        center = np.array([sx, sy, sz], np.float32)
        b = np.sum(rays*center, -1)
        discriminant = b*b-aa*(np.sum(center*center)-radius*radius)
        hit_z = (b-np.sqrt(np.maximum(discriminant, 0)))/aa
        mask = (discriminant > 0) & (hit_z > 0) & (hit_z < z)
        z[mask] = hit_z[mask]
        n = normalize(center-rays*hit_z[..., None])
        normal[mask] = n[mask]
        normal[mask & (normal[..., 2] < 0)] *= -1
        material[mask] = rgb
    # Subtle albedo pattern is shared by input and target, independent of lights.
    material *= (.92+.08*np.cos(rays[..., 0]*z*13)*np.cos(rays[..., 1]*z*13))[..., None]
    source = (material*illumination(normal)).astype(np.float32)
    target = (material*illumination(normal, True)).astype(np.float32)
    alpha = (.3+.7*x/max(width-1, 1))[..., None]
    color = np.concatenate([source, alpha], -1).astype(np.float32)
    return dict(depth=z, color=color, target=target, exact_normal=normal,
                fx=float(fx), fy=float(fy), cx=cx, cy=cy)


def normals_from_depth(z, fx, fy, cx, cy, limit=.02):
    y, x = np.mgrid[:z.shape[0], :z.shape[1]].astype(np.float32)
    positions = np.stack([(x-cx)/fx, (y-cy)/fy, np.ones_like(x)], -1)*z[..., None]
    with np.errstate(invalid='ignore', divide='ignore'):
        center = z[1:-1, 1:-1]
        neighbors = np.stack([z[1:-1, :-2], z[1:-1, 2:], z[:-2, 1:-1], z[2:, 1:-1]], -1)
        neighbor_ok = (np.isfinite(neighbors) & (neighbors > 0)
                       & (np.abs(neighbors-center[..., None]) <= limit*center[..., None]))
        mid = positions[1:-1, 1:-1]
        left = np.where(neighbor_ok[..., 0, None], positions[1:-1, :-2], mid)
        right = np.where(neighbor_ok[..., 1, None], positions[1:-1, 2:], mid)
        up = np.where(neighbor_ok[..., 2, None], positions[:-2, 1:-1], mid)
        down = np.where(neighbor_ok[..., 3, None], positions[2:, 1:-1], mid)
        cross = np.cross(right-left, down-up)
        length2 = np.sum(cross*cross, -1)
        n = cross/np.sqrt(np.maximum(length2, 1e-20))[..., None]
        n[n[..., 2] < 0] *= -1
        valid = (np.isfinite(center) & (center > 0)
                 & np.any(neighbor_ok[..., :2], -1) & np.any(neighbor_ok[..., 2:], -1)
                 & np.isfinite(length2) & (length2 > 1e-20))
    result = np.zeros((*z.shape, 3), np.float32); result[..., 2] = 1
    result[1:-1, 1:-1] = np.where(valid[..., None], n, [0, 0, 1])
    mask = np.zeros(z.shape, bool); mask[1:-1, 1:-1] = valid
    return result, mask


def prepare(root, model_path):
    root.mkdir(parents=True, exist_ok=True)
    with np.load(model_path, allow_pickle=False) as data:
        p = {k: data[k].copy() for k in ('w1', 'b1', 'w2', 'b2')}
    assert p['w1'].shape == (3, 24) and p['w2'].shape == (24, 3)
    packed = np.zeros((49, 4), np.float32)
    packed[:24, :3] = p['w1'].T; packed[:24, 3] = p['b1']
    packed[24:48, :3] = p['w2']; packed[48, :3] = p['b2']
    cases = [('preview', 640, 360, 1., False), ('1080p', 1920, 1080, 1., False),
             ('odd', 321, 181, 1., False), ('bypass', 65, 37, 0., True),
             ('invalid-depth', 65, 37, 1., True)]
    cases += [(f'motion-{i}', 320, 180, 1., False) for i in range(5)]
    report = dict(weights_sha256=hashlib.sha256(model_path.read_bytes()).hexdigest(), cases=[])
    for name, width, height, strength, corrupt in cases:
        folder = root/name; folder.mkdir(exist_ok=True)
        offset = (int(name.split('-')[1])-2)*.03 if name.startswith('motion-') else 0.
        scene = render(width, height, offset)
        if corrupt:
            scene['depth'][6:9, 6:9] = np.nan
            scene['depth'][12:15, 12:15] = np.inf
            scene['depth'][18:21, 18:21] = 0
            scene['depth'][24:27, 24:27] = -1
        normals, valid = normals_from_depth(scene['depth'], scene['fx'], scene['fy'], scene['cx'], scene['cy'])
        expected = scene['color'].copy()
        transformed = apply(p, normals, scene['color'][..., :3], strength)
        expected[..., :3] = np.where(valid[..., None], transformed, expected[..., :3])
        np.testing.assert_array_equal(expected[~valid], scene['color'][~valid])
        assert np.isfinite(expected).all()
        if strength == 0:
            np.testing.assert_array_equal(expected, scene['color'])
        for filename, array in [('color.bin', scene['color']), ('depth.bin', scene['depth']),
                                ('expected.bin', expected), ('weights.bin', packed)]:
            array.astype('<f4').tofile(folder/filename)
        (folder/'parameters.bin').write_bytes(struct.pack('<II6f', width, height, scene['fx'], scene['fy'], scene['cx'], scene['cy'], strength, .02))
        np.savez(folder/'reference.npz', target=scene['target'], exact_normal=scene['exact_normal'],
                 estimated_normal=normals, valid=valid)
        dot = np.sum(normals*scene['exact_normal'], -1)
        angles = np.rad2deg(np.arccos(np.clip(dot[valid], -1, 1)))
        mse = lambda a: float(np.mean((a.astype(np.float64)-scene['target'])**2))
        info = dict(name=name, width=width, height=height, strength=strength, object_offset=offset, valid_fraction=float(valid.mean()),
                    normal_angle_mean_deg=float(angles.mean()), normal_angle_p95_deg=float(np.percentile(angles, 95)),
                    input_target_mse=mse(scene['color'][..., :3]), depth_model_target_mse=mse(expected[..., :3]),
                    exact_normal_model_target_mse=mse(apply(p, scene['exact_normal'], scene['color'][..., :3], strength)))
        report['cases'].append(info)
    # Geometric oracle: a perspective sloped plane has one known normal.
    h, w = 93, 157
    yy, xx = np.mgrid[:h, :w].astype(np.float32)
    true_n = normalize(np.array([.2, -.3, 1], np.float32))
    rays = np.stack([(xx-78)/110, (yy-46)/110, np.ones_like(xx)], -1)
    depth = (3/(rays@true_n)).astype(np.float32)
    estimated, valid = normals_from_depth(depth, 110, 110, 78, 46)
    assert valid[1:-1, 1:-1].all()
    assert np.max(np.abs(estimated[valid]-true_n)) < 2e-5
    report['sloped_plane_oracle'] = 'passed'
    (root/'preparation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


def evaluate(root):
    report = json.loads((root/'preparation.json').read_text(encoding='utf-8'))
    motion = []
    for case in report['cases']:
        folder = root/case['name']; shape = (case['height'], case['width'], 4)
        gpu = np.fromfile(folder/'gpu.bin', '<f4').reshape(shape)
        source = np.fromfile(folder/'color.bin', '<f4').reshape(shape)
        expected = np.fromfile(folder/'expected.bin', '<f4').reshape(shape)
        with np.load(folder/'reference.npz', allow_pickle=False) as ref:
            target, valid = ref['target'], ref['valid']
        assert np.isfinite(gpu).all()
        np.testing.assert_array_equal(gpu[..., 3], source[..., 3])
        np.testing.assert_array_equal(gpu[~valid], source[~valid])
        np.testing.assert_allclose(gpu, expected, atol=2e-3, rtol=0)
        if case['strength'] == 0:
            np.testing.assert_array_equal(gpu, source)
        case['gpu_target_mse'] = float(np.mean((gpu[..., :3].astype(np.float64)-target)**2))
        case['gpu'] = json.loads((folder/'timing.json').read_text())
        if case['name'].startswith('motion-'):
            motion.append((source[..., :3], gpu[..., :3], target))
        if case['name'] in ('preview', '1080p', 'odd'):
            assert case['gpu_target_mse'] < case['input_target_mse']
        if case['name'] == 'preview':
            panels = [('Original', source[..., :3]), ('Our trained GPU model', gpu[..., :3]), ('Analytic target', target)]
            canvas = Image.new('RGB', (1280, 800), '#161c27'); draw = ImageDraw.Draw(canvas)
            draw.text((12, 8), 'EXPERIMENTAL - SYNTHETIC SCENE - not a game screenshot', fill='white')
            for i, (label, pixels) in enumerate(panels):
                x, y = (i%2)*640, 38+(i//2)*380
                display = np.clip(pixels, 0, 1)
                display = np.where(display <= .0031308, display*12.92, 1.055*display**(1/2.4)-.055)
                image = Image.fromarray(np.uint8(np.round(display*255)))
                image.save(root/('preview-'+str(i)+'.png'))
                canvas.paste(image, (x, y)); draw.text((x+8, y+362), label, fill='white')
            draw.text((656, 460), 'Depth-derived normals + trained 171-parameter model', fill='white')
            draw.text((656, 490), 'Fixed lighting. Unsupported/invalid depth preserves original.', fill='white')
            draw.text((656, 520), 'Known projection; no temporal history or game integration.', fill='white')
            canvas.save(root/'comparison.png')
    # Difference of consecutive frame differences versus the changing target.
    # This is a screen-space residual (includes disocclusions), not a perceptual
    # flicker score or proof of general temporal consistency.
    changes = []
    frames = []
    for i, (src, gpu, target) in enumerate(motion):
        pixels = np.concatenate([src, gpu, target], axis=1)
        pixels = np.clip(pixels, 0, 1)
        pixels = np.where(pixels<=.0031308, pixels*12.92, 1.055*pixels**(1/2.4)-.055)
        canvas = Image.new('RGB', (960, 210), '#161c27')
        canvas.paste(Image.fromarray(np.uint8(np.round(pixels*255))), (0, 30))
        draw = ImageDraw.Draw(canvas)
        for col, title in enumerate(['Synthetic input', 'Our GPU model', 'Analytic target']):
            draw.text((col*320+6, 8), title, fill='white')
        frames.append(canvas)
        if i:
            prev_src, prev_gpu, prev_target = motion[i-1]
            oracle_delta = target.astype(np.float64)-prev_target
            changes.append(dict(frame=i,
                input_change_error_mse=float(np.mean(((src.astype(np.float64)-prev_src)-oracle_delta)**2)),
                gpu_change_error_mse=float(np.mean(((gpu.astype(np.float64)-prev_gpu)-oracle_delta)**2))))
    if frames:
        frames[0].save(root/'motion-comparison.gif', save_all=True,
                       append_images=frames[1:]+frames[-2:0:-1], duration=180, loop=0)
    report['motion_screen_space_change_error'] = changes
    (root/'results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', 'evaluate'])
    parser.add_argument('--root', type=Path, default=Path('artifacts/relighting-depth-v1'))
    parser.add_argument('--weights', type=Path, default=Path('artifacts/relighting-fixture-v1/weights.npz'))
    args = parser.parse_args()
    prepare(args.root, args.weights) if args.mode == 'prepare' else evaluate(args.root)
