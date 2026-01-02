#version 330 core

in vec2 TexCoord;
out vec2 UV;

uniform sampler2D rgbTexture;

// BT.709 coefficients for U and V (chroma)
const vec3 uCoeff = vec3(-0.1146, -0.3854, 0.5);
const vec3 vCoeff = vec3(0.5, -0.4542, -0.0458);

void main() {
    // Sample 2x2 block and average (420 subsampling)
    vec2 pixelSize = 1.0 / textureSize(rgbTexture, 0);
    
    vec3 rgb00 = texture(rgbTexture, TexCoord).rgb;
    vec3 rgb10 = texture(rgbTexture, TexCoord + vec2(pixelSize.x, 0.0)).rgb;
    vec3 rgb01 = texture(rgbTexture, TexCoord + vec2(0.0, pixelSize.y)).rgb;
    vec3 rgb11 = texture(rgbTexture, TexCoord + pixelSize).rgb;
    
    vec3 rgbAvg = (rgb00 + rgb10 + rgb01 + rgb11) * 0.25;
    
    // Convert to U and V
    float u = dot(rgbAvg, uCoeff);
    float v = dot(rgbAvg, vCoeff);
    
    // Scale to [16, 240] range
    UV.x = 128.0/255.0 + (224.0/255.0) * u; // U
    UV.y = 128.0/255.0 + (224.0/255.0) * v; // V
}
