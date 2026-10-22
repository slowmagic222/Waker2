import Autotracker from './autotracker';
import LogicHelper from './logic-helper';
import Settings from './settings';
import TrackerState from './tracker-state';

describe('Autotracker', () => {
  let trackerState;

  const gameState = (overrides) => ({
    version: 1,
    game: 'GZLE99',
    playing: true,
    saveSlot: 0,
    items: {},
    smallKeysHeld: {},
    locations: {},
    salvagedCharts: [],
    ...overrides,
  });

  beforeEach(() => {
    jest.spyOn(LogicHelper, 'startingItemCount').mockReturnValue(0);
    jest.spyOn(LogicHelper, 'maxItemCount').mockImplementation((item) => ({
      'Progressive Sword': 4,
      'DRC Small Key': 4,
      'Wind Waker': 1,
    }[item]));
    jest.spyOn(Settings, 'getOptionValue').mockReturnValue(false);
    jest.spyOn(LogicHelper, 'islandForChart').mockImplementation((chart) => ({
      'Treasure Chart 25': 'Forsaken Fortress Sector',
    }[chart] || null));

    trackerState = TrackerState.createStateRaw({
      entrances: {},
      islandsForCharts: {},
      items: {
        'Progressive Sword': 0,
        'DRC Small Key': 0,
        'Wind Waker': 1,
      },
      itemsForLocations: {},
      locationsChecked: {
        'Outset Island': {
          "Underneath Link's House": false,
          "Mesa the Grasscutter's House": false,
        },
        'Forsaken Fortress Sector': {
          'Sunken Treasure': false,
        },
        'Star Island': {
          'Sunken Treasure': false,
        },
      },
    });
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  test('ignores a game that is not being played', () => {
    const result = Autotracker.apply(trackerState, { version: 1, playing: false }, null);

    expect(result).toBe(trackerState);
  });

  test('ignores another version of the state', () => {
    const result = Autotracker.apply(
      trackerState,
      gameState({ version: 2, items: { 'Progressive Sword': 2 } }),
      null,
    );

    expect(result).toBe(trackerState);
  });

  test('takes the higher count the first time', () => {
    const marked = trackerState.incrementItem('Progressive Sword').incrementItem('Progressive Sword');
    const first = Autotracker.apply(marked, gameState({ items: { 'Progressive Sword': 1 } }), null);

    expect(first.getItemValue('Progressive Sword')).toBe(2);
  });

  test('sets an item when the game count changes, and keeps manual marks otherwise', () => {
    const previous = gameState({ items: { 'Progressive Sword': 1, 'Wind Waker': 1 } });
    const once = Autotracker.apply(trackerState, previous, null);

    expect(once.getItemValue('Progressive Sword')).toBe(1);

    const marked = once.incrementItem('Progressive Sword');
    const unchanged = Autotracker.apply(marked, previous, previous);

    expect(unchanged).toBe(marked);
    expect(unchanged.getItemValue('Progressive Sword')).toBe(2);

    const next = gameState({ items: { 'Progressive Sword': 3, 'Wind Waker': 1 } });
    const updated = Autotracker.apply(unchanged, next, previous);

    expect(updated.getItemValue('Progressive Sword')).toBe(3);
  });

  test('clamps counts to the item maximum and ignores unknown items', () => {
    const result = Autotracker.apply(
      trackerState,
      gameState({ items: { 'Progressive Sword': 9, 'Not An Item': 1 } }),
      null,
    );

    expect(result.getItemValue('Progressive Sword')).toBe(4);
    expect(result.items).not.toHaveProperty('Not An Item');
  });

  test('adds the small keys the game gains and ignores keys used', () => {
    const start = gameState({ smallKeysHeld: { 'DRC Small Key': 1 } });
    const first = Autotracker.apply(trackerState, start, null);

    expect(first.getItemValue('DRC Small Key')).toBe(1);

    const used = gameState({ smallKeysHeld: { 'DRC Small Key': 0 } });
    const afterUse = Autotracker.apply(first, used, start);

    expect(afterUse.getItemValue('DRC Small Key')).toBe(1);

    const gained = gameState({ smallKeysHeld: { 'DRC Small Key': 1 } });
    const afterGain = Autotracker.apply(afterUse, gained, used);

    expect(afterGain.getItemValue('DRC Small Key')).toBe(2);
  });

  test('ignores small keys the tracker does not know', () => {
    const result = Autotracker.apply(
      trackerState,
      gameState({ smallKeysHeld: { 'XX Small Key': 1 } }),
      null,
    );

    expect(result).toBe(trackerState);
  });

  test('starts over when the save slot changes', () => {
    const slot0 = gameState({ items: { 'Progressive Sword': 1 } });
    const first = Autotracker.apply(trackerState, slot0, null);
    const marked = first.incrementItem('Progressive Sword').incrementItem('Progressive Sword');
    const slot1 = gameState({ saveSlot: 1, items: { 'Progressive Sword': 2 } });

    expect(Autotracker.apply(marked, slot1, slot0).getItemValue('Progressive Sword')).toBe(3);
  });

  test('checks locations and never unchecks them', () => {
    const marked = trackerState.toggleLocationChecked('Outset Island', "Mesa the Grasscutter's House");
    const result = Autotracker.apply(marked, gameState({
      locations: {
        'Outset Island': {
          "Underneath Link's House": true,
          "Mesa the Grasscutter's House": false,
        },
        'Not A Place': { Anything: true },
      },
    }), null);

    expect(result.isLocationChecked('Outset Island', "Underneath Link's House")).toBe(true);
    expect(result.isLocationChecked('Outset Island', "Mesa the Grasscutter's House")).toBe(true);
    expect(result.locationsChecked).not.toHaveProperty('Not A Place');
  });

  test('checks the sunken treasure of a salvaged chart through the chart mapping', () => {
    const vanilla = Autotracker.apply(
      trackerState,
      gameState({ salvagedCharts: ['Treasure Chart 25'] }),
      null,
    );

    expect(vanilla.isLocationChecked('Forsaken Fortress Sector', 'Sunken Treasure')).toBe(true);

    Settings.getOptionValue.mockReturnValue(true);
    const unmapped = Autotracker.apply(
      trackerState,
      gameState({ salvagedCharts: ['Treasure Chart 25'] }),
      null,
    );

    expect(unmapped).toBe(trackerState);

    const mapped = TrackerState.createStateRaw({
      ...trackerState.readState(),
      islandsForCharts: { 'Treasure Chart 25': 'Star Island' },
    });
    const result = Autotracker.apply(mapped, gameState({ salvagedCharts: ['Treasure Chart 25'] }), null);

    expect(result.isLocationChecked('Star Island', 'Sunken Treasure')).toBe(true);
    expect(result.isLocationChecked('Forsaken Fortress Sector', 'Sunken Treasure')).toBe(false);
  });

  test('finds a sunken treasure under the newer "Sector" names and the older ones', () => {
    LogicHelper.islandForChart.mockReturnValue('Forsaken Fortress');
    const newer = Autotracker.apply(
      trackerState,
      gameState({ salvagedCharts: ['Treasure Chart 25'] }),
      null,
    );

    expect(newer.isLocationChecked('Forsaken Fortress Sector', 'Sunken Treasure')).toBe(true);

    LogicHelper.islandForChart.mockReturnValue('Star Island Sector');
    const older = Autotracker.apply(
      trackerState,
      gameState({ salvagedCharts: ['Treasure Chart 25'] }),
      null,
    );

    expect(older.isLocationChecked('Star Island', 'Sunken Treasure')).toBe(true);

    LogicHelper.islandForChart.mockReturnValue('Nowhere');

    expect(Autotracker.apply(
      trackerState,
      gameState({ salvagedCharts: ['Treasure Chart 25'] }),
      null,
    )).toBe(trackerState);
  });
});
