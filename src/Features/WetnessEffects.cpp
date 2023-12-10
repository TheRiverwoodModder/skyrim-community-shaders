#include "WetnessEffects.h"
#include <Util.h>

const float MIN_START_PERCENTAGE = 0.05f;
const float DEFAULT_TRANSITION_PERCENTAGE = 1.0f;
const float TRANSITION_CURVE_MULTIPLIER = 2.0f;
const float TRANSITION_DENOMINATOR = 256.0f;
const float FOG_POWER_THRESHOLD = 0.5f;
const float FOG_NEAR_DISTANCE_THRESHOLD = 0.0f;
const float RAIN_WETNESS = 1.0f;
const float SNOW_WETNESS = 0.0f;
const float FOG_WETNESS = 0.5f;
const float DRY_WETNESS = 0.0f;
const float DAY = 0.0f;
const float NIGHT = 1.0f;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WetnessEffects::Settings,
	EnableWetnessEffects,
	MaxRainWetness,
	MaxShoreWetness,
	MaxDarkness,
	MaxOcclusion,
	MinRoughness,
	ShoreRange,
	PuddleMinWetness,
	PuddleRadius,
	PuddleMaxAngle,
	PuddleFlatness)

void WetnessEffects::DrawSettings()
{
	if (ImGui::TreeNodeEx("Wetness Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enable Wetness", (bool*)&settings.EnableWetnessEffects);
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::Text("Enables a wetness effect near water and when it is raining.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		ImGui::SliderFloat("Max Rain Wetness", &settings.MaxRainWetness, 0.0f, 1.0f);
		ImGui::SliderFloat("Max Shore Wetness", &settings.MaxShoreWetness, 0.0f, 1.0f);
		ImGui::SliderFloat("Max Darkeness", &settings.MaxDarkness, 1.0f, 2.0f);
		ImGui::SliderFloat("Max Occlusion", &settings.MaxOcclusion, 0.0f, 1.0f);
		ImGui::SliderFloat("Min Roughness", &settings.MinRoughness, 0.0f, 1.0f);

		ImGui::SliderInt("Shore Range", (int*)&settings.ShoreRange, 1, 64);

		ImGui::SliderFloat("Puddle Min Wetness", &settings.PuddleMinWetness, 0.0f, 1.0f);
		ImGui::SliderFloat("Puddle Radius", &settings.PuddleRadius, 0.0f, 3.0f);
		ImGui::SliderFloat("Puddle Max Angle", &settings.PuddleMaxAngle, 0.0f, 1.0f);
		ImGui::SliderFloat("Puddle Flatness", &settings.PuddleFlatness, 0.0f, 1.0f);

		ImGui::TreePop();
	}
}

float WetnessEffects::CalculateWeatherTransitionPercentage(RE::TESWeather* weather, float skyCurrentWeatherPct, float beginFade)
{
	float weatherTransitionPercentage = DEFAULT_TRANSITION_PERCENTAGE;
	if (weather) {
		// Correct if beginFade is zero or negative
		beginFade = beginFade > 0 ? beginFade : beginFade + TRANSITION_DENOMINATOR;
		// Wait to start transition until precipitation begins/ends
		float startPercentage = (TRANSITION_DENOMINATOR - beginFade) * (1.0f / TRANSITION_DENOMINATOR);
		startPercentage = startPercentage > MIN_START_PERCENTAGE ? startPercentage : MIN_START_PERCENTAGE;
		float currentPercentage = (skyCurrentWeatherPct - startPercentage) / (1 - startPercentage);
		weatherTransitionPercentage = std::clamp(currentPercentage, 0.0f, 1.0f);
	}
	return weatherTransitionPercentage;
}

float WetnessEffects::CalculateWetness(RE::TESWeather* weather, RE::Sky* sky)
{
	float wetness = DRY_WETNESS;
	if (weather && sky) {
		// Figure out the weather type and set the wetness
		if (weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
			// Raining
			wetness = RAIN_WETNESS;
			logger::debug("RAIN: Current Weather: {}, Fog Day Near {}, Fog Day Power {}, Fog Night Near {}, Fog Night Power {}, Day wetness {}, Night wetness {}, Wetness {}, DayNightTransition {}", weather->GetFormID(), weather->fogData.dayNear, weather->fogData.dayPower, weather->fogData.nightNear, weather->fogData.nightPower, wetness, wetness, wetness, "n/a");
		} else if (weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
			// Snowing
			wetness = SNOW_WETNESS;
			logger::debug("SNOW: Current Weather: {}, Fog Day Near {}, Fog Day Power {}, Fog Night Near {}, Fog Night Power {}, Day wetness {}, Night wetness {}, Wetness {}, DayNightTransition {}", weather->GetFormID(), weather->fogData.dayNear, weather->fogData.dayPower, weather->fogData.nightNear, weather->fogData.nightPower, wetness, wetness, wetness, "n/a");
		} else if ((weather->fogData.dayNear <= FOG_NEAR_DISTANCE_THRESHOLD && weather->fogData.dayPower <= FOG_POWER_THRESHOLD) ||
				   (weather->fogData.nightNear <= FOG_NEAR_DISTANCE_THRESHOLD && weather->fogData.nightPower <= FOG_POWER_THRESHOLD)) {
			// Foggy - could be foggy during day, night, or both
			float dayWetness = (weather->fogData.dayNear <= FOG_NEAR_DISTANCE_THRESHOLD && weather->fogData.dayPower <= FOG_POWER_THRESHOLD) ? FOG_WETNESS : DRY_WETNESS;
			float nightWetness = (weather->fogData.nightNear <= FOG_NEAR_DISTANCE_THRESHOLD && weather->fogData.nightPower <= FOG_POWER_THRESHOLD) ? FOG_WETNESS : DRY_WETNESS;
			// If both, skip interpolating between day and night values
			if (dayWetness == nightWetness) {
				wetness = FOG_WETNESS;
				logger::debug("FOG(both): Current Weather: {}, Fog Day Near {}, Fog Day Power {}, Fog Night Near {}, Fog Night Power {}, Day wetness {}, Night wetness {}, Wetness {}, DayNightTransition {}", weather->GetFormID(), weather->fogData.dayNear, weather->fogData.dayPower, weather->fogData.nightNear, weather->fogData.nightPower, dayWetness, nightWetness, wetness, "n/a");	
			} else {
				float DayNightTransition = DEFAULT_TRANSITION_PERCENTAGE;
				// Set the Day Night Transition with Day = 0, and Night = 1
				if (auto calendar = RE::Calendar::GetSingleton()) {
					if (auto currentClimate = sky->currentClimate) {
						struct tm sunriseBeginTM = currentClimate->timing.sunrise.GetBeginTime();
						struct tm sunriseEndTM = currentClimate->timing.sunrise.GetEndTime();
						struct tm sunsetBeginTM = currentClimate->timing.sunset.GetBeginTime();
						struct tm sunsetEndTM = currentClimate->timing.sunset.GetEndTime();
						float gameHour = calendar->GetHour();
						// Skyrim doesn't seem to use seconds, but doing this in seconds just in case
						int sunriseBeginTime = (sunriseBeginTM.tm_hour * 3600) + (sunriseBeginTM.tm_min * 60) + sunriseBeginTM.tm_sec;
						int sunriseEndTime = (sunriseEndTM.tm_hour * 3600) + (sunriseEndTM.tm_min * 60) + sunriseEndTM.tm_sec;
						int sunsetBeginTime = (sunsetBeginTM.tm_hour * 3600) + (sunsetBeginTM.tm_min * 60) + sunsetBeginTM.tm_sec;
						int sunsetEndTime = (sunsetEndTM.tm_hour * 3600) + (sunsetEndTM.tm_min * 60) + sunsetEndTM.tm_sec;
						int time = static_cast<int>(gameHour * 3600);

						if (sunsetEndTime > sunsetBeginTime && sunsetBeginTime >= sunriseEndTime && sunriseEndTime > sunriseBeginTime && sunriseBeginTime >= 0) {
							if ((time - sunriseBeginTime) < 0 || (time - sunsetEndTime) > 0) {
								// Night
								DayNightTransition = NIGHT;
								logger::debug("FOG Night");
							} else if ((time - sunriseEndTime) <= 0) {
								// During sunrise, night 1 -> day 0
								DayNightTransition = 1.0f - (static_cast<float>(time - sunriseBeginTime) / static_cast<float>(sunriseEndTime - sunriseBeginTime));
								logger::debug("FOG Sunrise");
							} else if ((time - sunsetBeginTime) < 0) {
								// During day
								DayNightTransition = DAY;
								logger::debug("FOG Day");
							} else if ((time - sunsetEndTime) <= 0) {
								// During sunset, day 0 -> night 1
								DayNightTransition = static_cast<float>(time - sunsetBeginTime) / static_cast<float>(sunsetEndTime - sunsetBeginTime);
								logger::debug("FOG Sunset");
							} else {
								// This should never happen
								logger::info("This shouldn't happen");
							}
						}
					}
				}
				// Handle wetness transition between day and night
				wetness = std::lerp(dayWetness, nightWetness, DayNightTransition);
				logger::debug("FOG(mixed): Current Weather: {}, Fog Day Near {}, Fog Day Power {}, Fog Night Near {}, Fog Night Power {}, Day wetness {}, Night wetness {}, Wetness {}, DayNightTransition {}", weather->GetFormID(), weather->fogData.dayNear, weather->fogData.dayPower, weather->fogData.nightNear, weather->fogData.nightPower, dayWetness, nightWetness, wetness, DayNightTransition);	
			}
		} else if (weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kCloudy)) {
			// Cloudy
			wetness = DRY_WETNESS;
			logger::debug("CLOUDY: Current Weather: {}, Fog Day Near {}, Fog Day Power {}, Fog Night Near {}, Fog Night Power {}, Day wetness {}, Night wetness {}, Wetness {}, DayNightTransition {}", weather->GetFormID(), weather->fogData.dayNear, weather->fogData.dayPower, weather->fogData.nightNear, weather->fogData.nightPower, wetness, wetness, wetness, "n/a");
		} else {
			// Pleasant
			wetness = DRY_WETNESS;
			logger::debug("CLEAR: Current Weather: {}, Fog Day Near {}, Fog Day Power {}, Fog Night Near {}, Fog Night Power {}, Day wetness {}, Night wetness {}, Wetness {}, DayNightTransition {}", weather->GetFormID(), weather->fogData.dayNear, weather->fogData.dayPower, weather->fogData.nightNear, weather->fogData.nightPower, wetness, wetness, wetness, "n/a");
		}
	}
	return wetness;
}

void WetnessEffects::Draw(const RE::BSShader* shader, const uint32_t)
{
	if (shader->shaderType.any(RE::BSShader::Type::Lighting)) {
		if (requiresUpdate) {
			requiresUpdate = false;
			auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

			PerPass data{};
			data.Wetness = DRY_WETNESS;

			if (settings.EnableWetnessEffects) {
				if (auto player = RE::PlayerCharacter::GetSingleton()) {
					if (auto cell = player->GetParentCell()) {
						if (!cell->IsInteriorCell()) {
							if (auto sky = RE::Sky::GetSingleton()) {
								if (auto currentWeather = sky->currentWeather) {
									float weatherTransitionPercentage = DEFAULT_TRANSITION_PERCENTAGE;
									float wetnessCurrentWeather = CalculateWetness(currentWeather, sky);
									float wetnessLastWeather = DRY_WETNESS;

									// If there is a lastWeather, figure out what type it is and set the wetness
									if (auto lastWeather = sky->lastWeather) {
										wetnessLastWeather = CalculateWetness(lastWeather, sky);

										// If it was raining, wait to transition until precipitation ends, otherwise use the current weather's fade in
										if (lastWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
											weatherTransitionPercentage = CalculateWeatherTransitionPercentage(lastWeather, sky->currentWeatherPct, lastWeather->data.precipitationEndFadeOut);
										} else {
											weatherTransitionPercentage = CalculateWeatherTransitionPercentage(currentWeather, sky->currentWeatherPct, currentWeather->data.precipitationBeginFadeIn);
										}
										// Adjust the transition curve to ease in/out of the transition
										weatherTransitionPercentage = (exp2(TRANSITION_CURVE_MULTIPLIER * log2(weatherTransitionPercentage)));
									}

									// Transition between CurrentWeather and LastWeather wetness values
									data.Wetness = std::lerp(wetnessLastWeather, wetnessCurrentWeather, weatherTransitionPercentage);
								}
							}
						}
					}
				}
			}

			auto& state = RE::BSShaderManager::State::GetSingleton();
			RE::NiTransform& dalcTransform = state.directionalAmbientTransform;
			Util::StoreTransform3x4NoScale(data.DirectionalAmbientWS, dalcTransform);

			data.settings = settings;

			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(perPass->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			size_t bytes = sizeof(PerPass);
			memcpy_s(mapped.pData, bytes, &data, bytes);
			context->Unmap(perPass->resource.get(), 0);

			ID3D11ShaderResourceView* views[1]{};
			views[0] = perPass->srv.get();
			context->PSSetShaderResources(22, ARRAYSIZE(views), views);
		}
	}
}

void WetnessEffects::SetupResources()
{
	D3D11_BUFFER_DESC sbDesc{};
	sbDesc.Usage = D3D11_USAGE_DYNAMIC;
	sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	sbDesc.StructureByteStride = sizeof(PerPass);
	sbDesc.ByteWidth = sizeof(PerPass);
	perPass = std::make_unique<Buffer>(sbDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = 1;
	perPass->CreateSRV(srvDesc);
}

void WetnessEffects::Reset()
{
	requiresUpdate = true;
}

void WetnessEffects::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void WetnessEffects::Save(json& o_json)
{
	o_json[GetName()] = settings;
}