//! Camera and view-projection matrices for DirectX 11 (Left-Handed coordinate system).

use hikari_animation::math::{Mat4, Vec3};

#[derive(Debug, Clone)]
pub struct Camera {
    pub position: Vec3,
    pub target: Vec3,
    pub up: Vec3,
    pub fov_y: f32,
    pub aspect: f32,
    pub near_z: f32,
    pub far_z: f32,
}

impl Default for Camera {
    fn default() -> Self {
        Self {
            position: Vec3::new(0.0, 10.0, -35.0),
            target: Vec3::new(0.0, 10.0, 0.0),
            up: Vec3::new(0.0, 1.0, 0.0),
            fov_y: 30.0f32.to_radians(),
            aspect: 16.0 / 9.0,
            near_z: 0.1,
            far_z: 1000.0,
        }
    }
}

impl Camera {
    pub fn new(position: Vec3, target: Vec3, aspect: f32) -> Self {
        Self {
            position,
            target,
            aspect,
            ..Default::default()
        }
    }

    /// DirectX 11 Left-Handed LookAt View Matrix
    pub fn view_matrix(&self) -> Mat4 {
        let z_axis = (self.target - self.position).normalize();
        let x_axis = self.up.cross(z_axis).normalize();
        let y_axis = z_axis.cross(x_axis);

        let tx = -x_axis.dot(self.position);
        let ty = -y_axis.dot(self.position);
        let tz = -z_axis.dot(self.position);

        Mat4 {
            m: [
                [x_axis.x, y_axis.x, z_axis.x, 0.0],
                [x_axis.y, y_axis.y, z_axis.y, 0.0],
                [x_axis.z, y_axis.z, z_axis.z, 0.0],
                [tx, ty, tz, 1.0],
            ],
        }
    }

    /// DirectX 11 Left-Handed Perspective Projection Matrix (Z range: 0.0 to 1.0)
    pub fn projection_matrix(&self) -> Mat4 {
        let h = 1.0 / (self.fov_y * 0.5).tan();
        let w = h / self.aspect;
        let q = self.far_z / (self.far_z - self.near_z);

        Mat4 {
            m: [
                [w, 0.0, 0.0, 0.0],
                [0.0, h, 0.0, 0.0],
                [0.0, 0.0, q, 1.0],
                [0.0, 0.0, -q * self.near_z, 0.0],
            ],
        }
    }

    pub fn view_projection_matrix(&self) -> Mat4 {
        self.view_matrix() * self.projection_matrix()
    }
}
