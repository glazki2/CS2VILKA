#pragma once

#include <cmath>
#include <cstddef>

namespace detection
{
	// One fired bullet: the angles the command fired with and the aim punch the server held before that bullet.
	struct RecoilSample
	{
		float pitch {};
		float yaw {};
		float punchPitch {};
		float punchYaw {};
	};

	struct RecoilScore
	{
		// Total bullet-to-bullet change of the recoil pattern itself, after weapon_recoil_scale.
		float punchVariation {};
		// What is left of that change once the player's own aim is added back in.
		float residualVariation {};
		// residualVariation / punchVariation. Close to 1 when the player does not follow every kick of the pattern,
		// close to 0 when every kick is cancelled exactly.
		float ratio {1.0f};
	};

	inline float WrapRecoilDegrees(float value)
	{
		return std::remainder(value, 360.0f);
	}

	// Bullets leave along view angles + aim punch * weapon_recoil_scale. Recoil compensation keeps that sum steady.
	// Second differences remove slow, smooth movement such as tracking a running enemy or pulling down evenly,
	// so only the bullet-to-bullet zig-zag of the pattern is compared. Human hands follow the overall trend of a
	// spray, but they cannot cancel each individual kick of the zig-zag; a recoil script can.
	inline RecoilScore ScoreRecoilCompensation(const RecoilSample *samples, std::size_t count, float recoilScale)
	{
		RecoilScore score;
		if (!samples || count < 3 || !(recoilScale > 0.0f) || !std::isfinite(recoilScale))
		{
			return score;
		}
		for (std::size_t index = 1; index + 1 < count; ++index)
		{
			const RecoilSample &previous = samples[index - 1];
			const RecoilSample &current = samples[index];
			const RecoilSample &next = samples[index + 1];
			const float punchPitch = recoilScale * ((next.punchPitch - current.punchPitch) - (current.punchPitch - previous.punchPitch));
			const float punchYaw =
				recoilScale * (WrapRecoilDegrees(next.punchYaw - current.punchYaw) - WrapRecoilDegrees(current.punchYaw - previous.punchYaw));
			const float aimPitch = (next.pitch - current.pitch) - (current.pitch - previous.pitch);
			const float aimYaw = WrapRecoilDegrees(next.yaw - current.yaw) - WrapRecoilDegrees(current.yaw - previous.yaw);
			score.punchVariation += std::hypot(punchPitch, punchYaw);
			score.residualVariation += std::hypot(aimPitch + punchPitch, aimYaw + punchYaw);
		}
		if (!std::isfinite(score.punchVariation) || !std::isfinite(score.residualVariation))
		{
			return {};
		}
		score.ratio = score.punchVariation > 0.0f ? score.residualVariation / score.punchVariation : 1.0f;
		return score;
	}
} // namespace detection
