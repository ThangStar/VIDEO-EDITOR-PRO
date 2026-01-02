#version 330 core

in vec2 TexCoord;
out float Y;

uniform sampler2D rgbTexture;

// BT.709 coefficients for Y (luma)
const vec3 yCoeff = vec3(0.2126, 0.7152, 0.0722);

void main() {
    vec3 rgb = texture(rgbTexture, TexCoord).rgb;
    
    // Convert RGB to Y (luma)
    // Y = 16 + 219 * (0.2126*R + 0.7152*G + 0.0722*B)
    float y = dot(rgb, yCoeff);
    
    // Scale to [16, 235] range for video
    Y = 16.0/255.0 + (219.0/255.0) * y;
}
