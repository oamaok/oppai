#include "diff_calc.h"
#include "common.h"

#include <cmath>
#include <algorithm>

#if NEEDS_TO_INSTALL_GENTOO
#include <functional> // std::greater
#endif

#include "utils.h"
#include "beatmap.h"

// based on tom94's osu!tp aimod
// TODO: reduce code redudnancy and rename variables to shorter names

namespace {
	// how much strains decay per interval (if the previous interval's peak
	// strains after applying decay are still higher than the current one's, 
	// they will be used as the peak strains).
	const f64 decay_base[] = { 0.3, 0.15 }; 

	// almost the normalized circle diameter (104px)
	const f64 almost_diameter = 90;

	// arbitrary tresholds to determine when a stream is spaced enough that is 
	// becomes hard to alternate.
	const f64 stream_spacing = 110;
	const f64 single_spacing = 125;

	// used to keep speed and aim balanced between eachother
	const f64 weight_scaling[] = { 1400, 26.25 };

	// step in milliseconds used to compute lazy movement for sliders
	const i64 slider_step = 10;

	// non-normalized diameter where the circlesize buff starts
	const f64 circlesize_buff_treshold = 30;

	namespace diff {
		const u8 speed = 0, 
				 aim = 1;
	}

	// diffcalc hit object
	struct d_obj {
		hit_object* ho;

		// strains start at 1
		f64 strains[2] = { 1, 1 };

		// start/end positions normalized on radius
		v2f norm_start;
		v2f norm_end;

		// length of the lazy movement for the 1st repeat
		f64 lazy_len_1st = 0; 

		// length of the lazy movement for each of the remaining repetitions
		f64 lazy_len_rest = 0; 

		void init(hit_object* base_object, f64 radius) {
			this->ho = base_object;

			// positions are normalized on circle radius so that we can calc as
			// if everything was the same circlesize
			f64 scaling_factor = 52.0 / radius;

			// cs buff (based on osuElements, not accurate)
			// TODO: find out real high CS buff
			if (radius < circlesize_buff_treshold) {
				scaling_factor *= std::min(1.1, 
					1 + (circlesize_buff_treshold - radius) * 0.02);
			}

			norm_start = ho->pos * scaling_factor;
			
			// just a circle bro
			if (ho->type != obj::slider) {
				norm_end = norm_start;
				return;
			}

			// compute the minimum lazy slider movement required to stay within
			// the follow circle.

			v2f cursor = ho->pos;
			f64 follow_circle_rad = radius * 3;

			auto& sl = ho->slider;
			i64 repetition_len = (ho->end_time - ho->time) / sl.repetitions;

			for (i64 t = slider_step; t < repetition_len; t+= slider_step) {
				v2f p = ho->at(t);
				if (err()) {
					return;
				}

				v2f d = p - cursor;
				f64 dist = d.len();

				if (dist <= follow_circle_rad) {
					continue;
				}

				d.norm();
				dist -= follow_circle_rad;
				cursor += d * dist;
				lazy_len_1st += dist;
			}

			lazy_len_1st *= scaling_factor;

			// -----------------------------------------------------------------
			lazy_len_1st = 0.0;
			// for some crazy reason the pp values match only if I entirely 
			// remove slider lengths, otherwise I get too much pp
			// TODO: ask tom about this, were slider length weights removed?
			// -----------------------------------------------------------------

			if (sl.repetitions % 2 == 1) {
				norm_end = cursor * scaling_factor;
				// end position = start position for odd amount of repetitions
			}
	
			if (sl.repetitions < 2) {
				return;
			}

			for (i64 t = repetition_len + slider_step;
					t < repetition_len * 2; t += slider_step) {
				
				v2f p = ho->at(t);
				if (err()) {
					return;
				}

				v2f d = p - cursor;
				f64 dist = d.len();

				if (dist <= follow_circle_rad) {
					continue;
				}

				d.norm();
				dist -= follow_circle_rad;
				cursor += d * dist;
				lazy_len_rest += dist;
			}

			lazy_len_rest *= scaling_factor;

			// -----------------------------------------------------------------
			lazy_len_rest = 0.0; // see above
			// -----------------------------------------------------------------

			if (sl.repetitions % 2 == 1) {
				norm_end = cursor * scaling_factor;
				// end position = start position for odd amount of repetitions
			}
		}

		void calculate_strains(d_obj& prev) {
			calculate_strain(prev, diff::speed);
			if (err()) {
				return;
			}

			calculate_strain(prev, diff::aim);
		}

		void calculate_strain(d_obj& prev, u8 dtype) {
			f64 res = 0;
			i64 time_elapsed = ho->time - prev.ho->time;
			f64 decay = pow(decay_base[dtype], time_elapsed / 1000.0);
			f64 scaling = weight_scaling[dtype];

			switch (ho->type) {
				case obj::circle:
					res = spacing_weight(distance(prev), dtype) * scaling;
					break;

				case obj::slider:
				{
					u16 reps = prev.ho->slider.repetitions - 1;

					switch (dtype) {
					case diff::speed:
						res = spacing_weight(
							prev.lazy_len_1st +
							prev.lazy_len_rest * reps +
							distance(prev),
							dtype
						) * scaling;
						break;

					case diff::aim:
						res = (
							spacing_weight(prev.lazy_len_1st, dtype) +
							spacing_weight(prev.lazy_len_rest, dtype) * reps +
							spacing_weight(distance(prev), dtype)
						) * scaling;
					}
					break;
				}

				case obj::spinner:
					break;

				case obj::invalid:
					die("Found invalid hit object");
					return;
			}

			res /= std::max(time_elapsed, (i64)50);
			strains[dtype] = prev.strains[dtype] * decay + res;
		}

		f64 spacing_weight(f64 distance, u8 diff_type) {
			switch (diff_type) {
				case diff::speed:
					if (distance > single_spacing) {
						return 2.5;
					}
					else if (distance > stream_spacing) {
						return 1.6 + 0.9 *
							(distance - stream_spacing) /
							(single_spacing - stream_spacing);
					}
					else if (distance > almost_diameter) {
						return 1.2 + 0.4 * (distance - almost_diameter)
							/ (stream_spacing - almost_diameter);
					}
					else if (distance > almost_diameter / 2.0) {
						return 0.95 + 0.25 * 
							(distance - almost_diameter / 2.0) /
							(almost_diameter / 2.0);
					}
					return 0.95;

				case diff::aim:
					return pow(distance, 0.99);

				default:
					return 0.0;
			}
		}

		f64 distance(d_obj& prev) {
			return (norm_start - prev.norm_end).len();
		}
	};

	const f64 star_scaling_factor = 0.0675;
	const f64 extreme_scaling_factor = 0.5;
	const f64 playfield_width = 512; // in osu!pixels

	// strains are calculated by analyzing the map in chunks and then taking the
	// peak strains in each chunk.
	// this is the length of a strain interval in milliseconds.
	const i64 strain_step = 400;

	// max strains are weighted from highest to lowest, and this is how much the
	// weight decays.
	const f64 decay_weight = 0.9;

	d_obj objects[beatmap::max_objects];
	size_t num_objects;

	f64 calculate_difficulty(u8 type) {
		std::vector<f64> highest_strains;
		i64 interval_end = strain_step;
		f64 max_strain = 0.0;

		d_obj* prev = nullptr;
		for (size_t i = 0; i < num_objects; i++) {
			auto& o = objects[i];

			// make previous peak strain decay until the current object
			while (o.ho->time > interval_end) {
				highest_strains.push_back(max_strain);

				if (!prev) {
					max_strain = 0.0;
				} else {
					f64 decay = pow(decay_base[type], 
						(interval_end - prev->ho->time) / 1000.0);
					max_strain = prev->strains[type] * decay;
				}

				interval_end += strain_step;
			}
		
			// calculate max strain for this interval
			max_strain = std::max(max_strain, o.strains[type]);
			prev = &o;
		}

		f64 difficulty = 0;
		f64 weight = 1.0;

		// sort strains from greatest to lowest
		std::sort(highest_strains.begin(), highest_strains.end(), 
			std::greater<f64>());

		// weigh the top strains
		for (const f64& strain : highest_strains) {
			difficulty += weight * strain;
			weight *= decay_weight;
		}

		return difficulty;
	}
}

f64 d_calc(beatmap& b, f64* aim, f64* speed) {
	dbgputs("\ndiff calc");

	if (b.mode != 0) {
		die("This gamemode is not supported");
		return 0;
	}

	f64 circle_radius = (playfield_width / 16.f) * (1.f - 0.7f *
			(b.cs - 5.f) / 5.f);

	dbgprintf("circle radius: %g\n", circle_radius);

	num_objects = b.num_objects;
	dbgputs("initializing objects");
	for (size_t i = 0; i < b.num_objects; i++) {
		objects[i].init(&b.objects[i], circle_radius);
		if (err()) {
			return 0;
		}
	}

	auto* prev = &objects[0];
	for (size_t i = 1; i < b.num_objects; i++) {
		auto& o = objects[i];

		o.calculate_strains(*prev);
		if (err()) {
			return 0;
		}

#if _DEBUG
		v2f endpos = o.ho->at(o.ho->end_time - o.ho->time);
		if (err()) {
			return 0;
		}
#endif

		dbgprintf("%" fi64 ": type %" fi32 ", strains %g %g, slider lens %g %g, "
				"norm pos %s-%s, pos %s-%s\n", 
				o.ho->time, (int)o.ho->type, o.strains[0], o.strains[1], 
				o.lazy_len_1st, o.lazy_len_rest,
				o.norm_start.str(), o.norm_end.str(), o.ho->pos.str(), 
				endpos.str());
		prev = &o;
	}

	*aim = calculate_difficulty(diff::aim);
	*speed = calculate_difficulty(diff::speed);
	*aim = sqrt(*aim) * star_scaling_factor;
	*speed = sqrt(*speed) * star_scaling_factor;

	// round to 2 decimal places
	*aim = std::round(*aim * 100.0) / 100.0;
	*speed = std::round(*speed * 100.0) / 100.0;

	f64 stars = *aim + *speed + 
		std::abs(*speed - *aim) * extreme_scaling_factor;

	stars = std::round(stars * 100.0) / 100.0;

	return stars;
}
