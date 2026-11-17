#pragma once
// The streets' range and numbers by the server's spawn_scale, ped_density and car_density (docs/FORK.md, "Wider streets").
namespace coop::streets {

// CPopCycle::UpdatePercentages has just counted the people and cars the zone should have: multiplies them
void ScaleCounts();

// as applied last: the range, and the numbers of people and of cars against the game's own
float Range();
float People();
float Traffic();

}  // namespace coop::streets
