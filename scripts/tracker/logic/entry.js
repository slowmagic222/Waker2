// The tracker's logic for the game itself (docs/TRACKER.md, "In the game"): wooferzfg's tracker's own
// services, bundled by scripts/tracker/build_logic.sh into one script the game runs in QuickJS. No UI,
// no network: the game passes the seed's permalink, wwrando's logic files (bundled with the game) and
// its state (pc_tracker.cpp's JSON); this answers which locations are checked, in logic or not.
//
//   cosTracker.init(permalink, itemLocationsText, macrosText) -> JSON {version, error?}
//   cosTracker.update(gameStateJson) -> JSON {areas (the 49 islands in sea chart order, the 7 dungeons,
//                                     then Mailbox, The Great Sea, Hyrule): [{name, dungeon, available, remaining, color,
//                                     locations: [{name, color}]}], checked, available, remaining}
//   cosTracker.updateText(gameStateJson) -> the same as lines, for the game (no JSON parser there):
//     T <checked> <available> <remaining>
//     A <dungeon 0|1> <available> <remaining> <color> <area name>
//     L <color> <location name>                  (the area's locations, after its A line)
//     I <count> <item name>                      (the tracker's item counts, from the game)
//     E <error>
//   fields separated by tabs, colors by their first letter (a, u, c, n);
//   colors: the tracker's, "available-location", "unavailable-location", "checked-location",
//   "non-progress-location"
import yaml from 'js-yaml';
import _ from 'lodash';

import Autotracker from './services/autotracker';
import Locations from './services/locations';
import LogicCalculation from './services/logic-calculation';
import LogicHelper from './services/logic-helper';
import LogicTweaks from './services/logic-tweaks';
import Macros from './services/macros';
import Settings from './services/settings';
import TrackerState from './services/tracker-state';

let trackerState = null;
let previousGame = null;

function areaSummary(logic, name, isDungeon, onlyProgressLocations) {
  const counts = logic.locationCounts(name, { isDungeon, onlyProgressLocations, disableLogic: false });
  const locations = logic.locationsList(name, { isDungeon, onlyProgressLocations, disableLogic: false });
  return {
    name,
    dungeon: isDungeon,
    available: counts.numAvailable,
    remaining: counts.numRemaining,
    color: counts.color,
    locations: _.map(locations, ({ location, color }) => ({ name: location, color })),
  };
}

const colorLetter = (color) => _.first(color);

globalThis.cosTracker = {
  init(permalink, itemLocationsText, macrosText) {
    try {
      Locations.reset();
      LogicHelper.reset();
      Macros.reset();
      Settings.reset();
      Settings.initializeFromPermalink(permalink);
      Locations.initialize(yaml.load(itemLocationsText));
      Macros.initialize(yaml.load(macrosText));
      LogicTweaks.applyTweaks();
      LogicHelper.initialize();
      trackerState = TrackerState.default();
      previousGame = null;
      return JSON.stringify({ version: Settings.getVersion() });
    } catch (err) {
      trackerState = null;
      return JSON.stringify({ error: String(err && err.stack ? err.stack : err) });
    }
  },

  update(gameStateJson, onlyProgressLocations = true) {
    if (_.isNil(trackerState)) {
      return JSON.stringify({ error: 'not initialized' });
    }
    if (!_.isEmpty(gameStateJson)) {
      const game = JSON.parse(gameStateJson);
      trackerState = Autotracker.apply(trackerState, game, previousGame);
      previousGame = game;
    }

    const logic = new LogicCalculation(trackerState);
    const areas = [];
    _.forEach(LogicHelper.ISLANDS, (island) => {
      areas.push(areaSummary(logic, island, false, onlyProgressLocations));
    });
    _.forEach(LogicHelper.DUNGEONS, (dungeon) => {
      areas.push(areaSummary(logic, dungeon, true, onlyProgressLocations));
    });
    _.forEach(LogicHelper.MISC_LOCATIONS, (location) => {
      areas.push(areaSummary(logic, location, false, onlyProgressLocations));
    });
    return JSON.stringify({
      areas,
      checked: logic.totalLocationsChecked({ onlyProgressLocations }),
      available: logic.totalLocationsAvailable({ onlyProgressLocations }),
      remaining: logic.totalLocationsRemaining({ onlyProgressLocations }),
    });
  },

  updateText(gameStateJson, onlyProgressLocations = true) {
    const result = JSON.parse(this.update(gameStateJson, onlyProgressLocations));
    if (!_.isNil(result.error)) {
      return `E\t${_.replace(result.error, /\s+/g, ' ')}\n`;
    }
    const lines = [`T\t${result.checked}\t${result.available}\t${result.remaining}`];
    _.forEach(result.areas, (area) => {
      lines.push(`A\t${area.dungeon ? 1 : 0}\t${area.available}\t${area.remaining}\t${colorLetter(area.color)}\t${area.name}`);
      _.forEach(area.locations, (location) => {
        lines.push(`L\t${colorLetter(location.color)}\t${location.name}`);
      });
    });
    _.forEach(trackerState.items, (count, name) => {
      lines.push(`I\t${count}\t${name}`);
    });
    return `${lines.join('\n')}\n`;
  },
};
