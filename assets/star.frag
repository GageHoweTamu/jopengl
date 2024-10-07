#version 330 core
out vec4 FragColor;

in vec3 ourColor;
in vec3 Normal;
in vec3 FragPos;

uniform vec3 color;
uniform vec3 lightPos;
uniform vec3 viewPos;

void main()
{
	// Ambient lighting
	float ambientStrength = 0.1;
	vec3 ambient = ambientStrength * vec3(1.0, 1.0, 1.0);

	// Diffuse lighting
	vec3 norm = normalize(Normal);
	vec3 lightDir = normalize(lightPos - FragPos);
	float diff = max(dot(norm, lightDir), 0.0);
	vec3 diffuse = diff * vec3(1.0, 1.0, 1.0);

	// Specular lighting
	float specularStrength = 0.5;
	vec3 viewDir = normalize(viewPos - FragPos);
	vec3 reflectDir = reflect(-lightDir, norm);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
	vec3 specular = specularStrength * spec * vec3(1.0, 1.0, 1.0);

	vec3 result = (ambient + diffuse + specular) * color * ourColor;
	FragColor = vec4(result, 1.0);
}