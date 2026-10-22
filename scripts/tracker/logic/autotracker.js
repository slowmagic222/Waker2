import _ from 'lodash';

import LogicHelper from './logic-helper';
import Permalink from './permalink';
import Settings from './settings';
import TrackerState from './tracker-state';

// The game's state merged into the tracker's (docs/TRACKER.md, "In the game"): what the game's save
// holds, as the JSON pc_tracker.cpp builds (version 1):
//   items           item counts in the tracker's names; every item the game can tell, 0 included
//   smallKeysHeld   small keys held now per dungeon (the game does not count the keys used)
//   locations       { generalLocation: { detailedLocation: checked } } for the ones it can tell
//   salvagedCharts  the charts whose sunken treasure was salvaged
// The game's state is merged into the tracker's, so marks made by hand stay: an item is set when
// the game's count changed since the last state applied (the first time, the higher of the two),
// a small key count gains what the game's count gains, and locations are only ever checked,
// never unchecked.
class Autotracker {
  static VERSION = 1;

  static apply(trackerState, gameState, previousGameState) {
    if (!_.get(gameState, 'playing') || _.get(gameState, 'version') !== this.VERSION) {
      return trackerState;
    }

    let previous = previousGameState;
    if (
      _.isNil(previous)
      || !previous.playing
      || previous.saveSlot !== gameState.saveSlot
      || previous.game !== gameState.game
    ) {
      previous = null;
    }

    const newItems = _.clone(trackerState.items);
    const newLocationsChecked = _.cloneDeep(trackerState.locationsChecked);
    let changed = false;

    const setItem = (itemName, count) => {
      const clamped = _.clamp(
        count,
        LogicHelper.startingItemCount(itemName),
        LogicHelper.maxItemCount(itemName),
      );
      if (newItems[itemName] !== clamped) {
        newItems[itemName] = clamped;
        changed = true;
      }
    };

    _.forEach(gameState.items, (count, itemName) => {
      if (!_.has(newItems, itemName)) {
        return;
      }
      if (_.isNil(previous)) {
        setItem(itemName, Math.max(newItems[itemName], count));
      } else if (_.get(previous, ['items', itemName]) !== count) {
        setItem(itemName, count);
      }
    });

    _.forEach(gameState.smallKeysHeld, (held, itemName) => {
      if (!_.has(newItems, itemName)) {
        return;
      }
      if (_.isNil(previous)) {
        setItem(itemName, Math.max(newItems[itemName], held));
        return;
      }
      const gained = held - _.get(previous, ['smallKeysHeld', itemName], held);
      if (gained > 0) {
        setItem(itemName, newItems[itemName] + gained);
      }
    });

    const check = (generalLocation, detailedLocation) => {
      const path = [generalLocation, detailedLocation];
      if (_.has(newLocationsChecked, path) && !_.get(newLocationsChecked, path)) {
        _.set(newLocationsChecked, path, true);
        changed = true;
      }
    };

    _.forEach(gameState.locations, (detailedLocations, generalLocation) => {
      _.forEach(detailedLocations, (isChecked, detailedLocation) => {
        if (isChecked) {
          check(generalLocation, detailedLocation);
        }
      });
    });

    _.forEach(gameState.salvagedCharts, (chart) => {
      const island = this.islandForSalvagedChart(trackerState, chart);
      if (_.isNil(island)) {
        return;
      }
      // wwrando's newer logic names two of them "Forsaken Fortress Sector" and "Tower of the Gods
      // Sector"; the tracker's island list has the older names
      const generalLocation = _.find(
        [island, `${island} Sector`, _.replace(island, / Sector$/, '')],
        (name) => _.has(newLocationsChecked, [name, 'Sunken Treasure']),
      );
      if (!_.isNil(generalLocation)) {
        check(generalLocation, 'Sunken Treasure');
      }
    });

    if (!changed) {
      return trackerState;
    }

    return TrackerState.createStateRaw({
      ...trackerState.readState(),
      items: newItems,
      locationsChecked: newLocationsChecked,
    });
  }

  // The island a chart leads to: the chart mapping entered in the tracker, else the chart's own
  // island unless the charts are randomized
  static islandForSalvagedChart(trackerState, chart) {
    const mappedIsland = trackerState.getIslandFromChartMapping(chart);
    if (!_.isNil(mappedIsland)) {
      return mappedIsland;
    }
    if (Settings.getOptionValue(Permalink.OPTIONS.RANDOMIZE_CHARTS)) {
      return null;
    }
    return LogicHelper.islandForChart(chart);
  }
}

export default Autotracker;
