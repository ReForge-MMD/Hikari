//! Exact MMD cubic Bézier curve evaluator for VMD keyframe interpolation.

/// Evaluates a cubic Bézier curve defined by two normalized control points (x1, y1) and (x2, y2),
/// where (x0, y0) = (0, 0) and (x3, y3) = (1, 1).
/// Control values are in range [0..127] as stored in VMD interpolation buffers.
#[inline]
pub fn eval_mmd_bezier(x1_byte: u8, y1_byte: u8, x2_byte: u8, y2_byte: u8, t: f32) -> f32 {
    if t <= 0.0 {
        return 0.0;
    }
    if t >= 1.0 {
        return 1.0;
    }

    let x1 = x1_byte as f32 / 127.0;
    let y1 = y1_byte as f32 / 127.0;
    let x2 = x2_byte as f32 / 127.0;
    let y2 = y2_byte as f32 / 127.0;

    // Linear optimization
    if (x1 - y1).abs() < 1e-4 && (x2 - y2).abs() < 1e-4 {
        return t;
    }

    // Solve for parameter s such that BezierX(s) == t using Newton-Raphson
    let mut s = t;
    for _ in 0..16 {
        let one_minus_s = 1.0 - s;
        // BezierX(s) = 3*(1-s)^2 * s * x1 + 3*(1-s) * s^2 * x2 + s^3
        let x = 3.0 * one_minus_s * one_minus_s * s * x1
            + 3.0 * one_minus_s * s * s * x2
            + s * s * s;
        let dx = x - t;
        if dx.abs() < 1e-5 {
            break;
        }

        // Derivative dBezierX/ds = 3*(1-s)^2 * x1 + 6*(1-s)*s * (x2 - x1) + 3*s^2 * (1 - x2)
        let d = 3.0 * one_minus_s * one_minus_s * x1
            + 6.0 * one_minus_s * s * (x2 - x1)
            + 3.0 * s * s * (1.0 - x2);
        if d.abs() < 1e-6 {
            break;
        }
        s -= dx / d;
        s = s.clamp(0.0, 1.0);
    }

    // Now evaluate BezierY(s)
    let one_minus_s = 1.0 - s;
    3.0 * one_minus_s * one_minus_s * s * y1
        + 3.0 * one_minus_s * s * s * y2
        + s * s * s
}

/// Unpacks interpolation parameters from 64-byte VMD bone interpolation buffer.
/// Channel: 0 = X, 1 = Y, 2 = Z, 3 = Rotation
#[inline]
pub fn unpack_bone_bezier(buf: &[u8; 64], channel: usize) -> (u8, u8, u8, u8) {
    // In VMD 64-byte layout:
    // [0..16]: X_x1, Y_x1, Z_x1, R_x1, X_y1, Y_y1, Z_y1, R_y1, X_x2, Y_x2, Z_x2, R_x2, X_y2, Y_y2, Z_y2, R_y2
    // Repeated 4 times across the 64 bytes.
    let x1 = buf[channel];
    let y1 = buf[4 + channel];
    let x2 = buf[8 + channel];
    let y2 = buf[12 + channel];
    (x1, y1, x2, y2)
}
