struct PhongMaterial {
	float4 modelColor;
	float ka;
	float kd;
	float ks;
	float shininess;
	bool hasDiffuseTexture; // TODO : Pack flags
	bool hasNormalTexture;
	bool hasSpecularTexture;
};
#include "Common.hlsl"

struct PhongInput {
	PhongMaterial mat;
	float4 diffuseColor;
	float3 normal;
	float3 specMap;
	float3 fragToCam;
	LightList lights;
};


float4 phongShade(PhongInput input) {

	float3 ambientCoefficient = float3(0.01f, 0.01f, 0.01f);

	float3 totalColor = float3(0.f, 0.f, 0.f);

	input.fragToCam = normalize(input.fragToCam);
	input.normal = normalize(input.normal);

	// Directional light

	input.lights.dirLight.direction = normalize(input.lights.dirLight.direction);

	float diffuseCoefficient = saturate(dot(input.normal, -input.lights.dirLight.direction));

	float3 specularCoefficient = float3(0.f, 0.f, 0.f);
	if (diffuseCoefficient > 0.f) {

		float3 r = reflect(input.lights.dirLight.direction, input.normal);
		r = normalize(r);
		specularCoefficient = pow(saturate(dot(input.fragToCam, r)), input.mat.shininess) * input.specMap;

	}
	totalColor += (input.mat.kd * diffuseCoefficient + input.mat.ks * specularCoefficient) * input.diffuseColor.rgb * input.lights.dirLight.color;

	// Point lights

	for (int i = 0; i < NUM_POINT_LIGHTS; i++) {
		PointLight p = input.lights.pointLights[i];

		p.fragToLight = normalize(p.fragToLight);

		diffuseCoefficient = saturate(dot(input.normal, p.fragToLight));

		specularCoefficient = float3(0.f, 0.f, 0.f);
		if (diffuseCoefficient > 0.f) {

			float3 r = reflect(-p.fragToLight, input.normal);
			r = normalize(r);
			specularCoefficient = pow(saturate(dot(input.fragToCam, r)), input.mat.shininess) * input.specMap;

		}

		//float attenuation = 1.f / (1.f + p.attenuation * pow(p.distanceToLight, 2.f));
        float attenuation = 1.f / (p.attConstant + p.attLinear * p.distanceToLight + p.attQuadratic * pow(p.distanceToLight, 2.f));

		totalColor += (input.mat.kd * diffuseCoefficient + input.mat.ks * specularCoefficient) * input.diffuseColor.rgb * p.color * attenuation;

	}



	return float4(saturate(input.mat.ka * ambientCoefficient * input.diffuseColor.rgb + totalColor), 1.0f);

}