/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = [
  "_RemoteSettingsExperimentLoader",
  "RemoteSettingsExperimentLoader",
  "RemoteDefaultsLoader",
];

const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
const { XPCOMUtils } = ChromeUtils.import(
  "resource://gre/modules/XPCOMUtils.jsm"
);

XPCOMUtils.defineLazyModuleGetters(this, {
  ASRouterTargeting: "resource://activity-stream/lib/ASRouterTargeting.jsm",
  TargetingContext: "resource://messaging-system/targeting/Targeting.jsm",
  ExperimentManager: "resource://nimbus/lib/ExperimentManager.jsm",
  RemoteSettings: "resource://services-settings/remote-settings.js",
  CleanupManager: "resource://normandy/lib/CleanupManager.jsm",
});

XPCOMUtils.defineLazyGetter(this, "log", () => {
  const { Logger } = ChromeUtils.import(
    "resource://messaging-system/lib/Logger.jsm"
  );
  return new Logger("RSLoader");
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "timerManager",
  "@mozilla.org/updates/timer-manager;1",
  "nsIUpdateTimerManager"
);

const COLLECTION_ID_PREF = "messaging-system.rsexperimentloader.collection_id";
const COLLECTION_ID_FALLBACK = "nimbus-desktop-experiments";
// TODO: Create a real collection
const COLLECTION_REMOTE_DEFAULTS = "nimbus-desktop-defaults";
const ENABLED_PREF = "messaging-system.rsexperimentloader.enabled";
const STUDIES_OPT_OUT_PREF = "app.shield.optoutstudies.enabled";

const TIMER_NAME = "rs-experiment-loader-timer";
const TIMER_LAST_UPDATE_PREF = `app.update.lastUpdateTime.${TIMER_NAME}`;
// Use the same update interval as normandy
const RUN_INTERVAL_PREF = "app.normandy.run_interval_seconds";

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "COLLECTION_ID",
  COLLECTION_ID_PREF,
  COLLECTION_ID_FALLBACK
);

/**
 * Responsible for pre-fetching remotely defined configurations from
 * Remote Settings.
 */
const RemoteDefaultsLoader = {
  _initialized: false,

  async loadRemoteDefaults() {
    if (!this._initialized) {
      log.debug("Fetching remote defaults for NimbusFeatures.");
      this._initialized = true;
      try {
        this._onUpdatesReady(await this._remoteSettingsClient.get());
      } catch (e) {
        Cu.reportError(e);
      }
      log.debug("Finished fetching remote defaults.");
    }
  },

  async _onUpdatesReady(remoteDefaults = []) {
    if (!remoteDefaults.length) {
      return;
    }
    await ExperimentManager.store.ready();
    const targetingContext = new TargetingContext();
    // Iterate over remote defaults: at most 1 per feature
    for (let remoteDefault of remoteDefaults) {
      if (!remoteDefault.configurations) {
        continue;
      }
      // Iterate over feature configurations and apply first which matches targeting
      for (let configuration of remoteDefault.configurations) {
        let result;
        try {
          result = await targetingContext.eval(configuration.targeting);
        } catch (e) {
          Cu.reportError(e);
        }
        if (result) {
          log.debug(
            `Setting remote defaults for feature: ${
              remoteDefault.id
            }: ${JSON.stringify(configuration)}`
          );
          ExperimentManager.store.updateRemoteConfigs(
            remoteDefault.id,
            configuration
          );
          break;
        }
      }
    }
  },
};

XPCOMUtils.defineLazyGetter(RemoteDefaultsLoader, "_remoteSettingsClient", () =>
  RemoteSettings(COLLECTION_REMOTE_DEFAULTS)
);

class _RemoteSettingsExperimentLoader {
  constructor() {
    // Has the timer been set?
    this._initialized = false;
    // Are we in the middle of updating recipes already?
    this._updating = false;

    // Make it possible to override for testing
    this.manager = ExperimentManager;

    XPCOMUtils.defineLazyGetter(this, "remoteSettingsClient", () => {
      return RemoteSettings(COLLECTION_ID);
    });

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "enabled",
      ENABLED_PREF,
      false,
      this.onEnabledPrefChange.bind(this)
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "studiesEnabled",
      STUDIES_OPT_OUT_PREF,
      false,
      this.onEnabledPrefChange.bind(this)
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "intervalInSeconds",
      RUN_INTERVAL_PREF,
      21600,
      () => this.setTimer()
    );
  }

  async init() {
    if (this._initialized || !this.enabled || !this.studiesEnabled) {
      return;
    }

    this.setTimer();
    CleanupManager.addCleanupHandler(() => this.uninit());
    this._initialized = true;

    await Promise.all([
      this.updateRecipes(),
      RemoteDefaultsLoader.loadRemoteDefaults(),
    ]);
  }

  uninit() {
    if (!this._initialized) {
      return;
    }
    timerManager.unregisterTimer(TIMER_NAME);
    this._initialized = false;
    this._updating = false;
  }

  async evaluateJexl(jexlString, customContext) {
    if (customContext && !customContext.experiment) {
      throw new Error(
        "Expected an .experiment property in second param of this function"
      );
    }

    const context = TargetingContext.combineContexts(
      customContext,
      this.manager.createTargetingContext(),
      ASRouterTargeting.Environment
    );

    log.debug("Testing targeting expression:", jexlString);
    const targetingContext = new TargetingContext(context);

    let result = false;
    try {
      result = await targetingContext.evalWithDefault(jexlString);
    } catch (e) {
      log.debug("Targeting failed because of an error");
      Cu.reportError(e);
    }
    return result;
  }

  /**
   * Checks targeting of a recipe if it is defined
   * @param {Recipe} recipe
   * @param {{[key: string]: any}} customContext A custom filter context
   * @returns {Promise<boolean>} Should we process the recipe?
   */
  async checkTargeting(recipe) {
    if (!recipe.targeting) {
      log.debug("No targeting for recipe, so it matches automatically");
      return true;
    }

    const result = await this.evaluateJexl(recipe.targeting, {
      experiment: recipe,
    });

    return Boolean(result);
  }

  /**
   * Get all recipes from remote settings
   * @param {string} trigger What caused the update to occur?
   */
  async updateRecipes(trigger) {
    if (this._updating || !this._initialized) {
      return;
    }
    this._updating = true;

    log.debug("Updating recipes" + (trigger ? ` with trigger ${trigger}` : ""));

    let recipes;
    let loadingError = false;

    try {
      recipes = await this.remoteSettingsClient.get();
      log.debug(`Got ${recipes.length} recipes from Remote Settings`);
    } catch (e) {
      log.debug("Error getting recipes from remote settings.");
      loadingError = true;
      Cu.reportError(e);
    }

    let matches = 0;
    if (recipes && !loadingError) {
      for (const r of recipes) {
        if (await this.checkTargeting(r)) {
          matches++;
          log.debug(`${r.id} matched`);
          await this.manager.onRecipe(r, "rs-loader");
        } else {
          log.debug(`${r.id} did not match due to targeting`);
        }
      }

      log.debug(`${matches} recipes matched. Finalizing ExperimentManager.`);
      this.manager.onFinalize("rs-loader");
    }

    if (trigger !== "timer") {
      const lastUpdateTime = Math.round(Date.now() / 1000);
      Services.prefs.setIntPref(TIMER_LAST_UPDATE_PREF, lastUpdateTime);
    }

    this._updating = false;
  }

  /**
   * Handles feature status based on feature pref and STUDIES_OPT_OUT_PREF.
   * Changing any of them to false will turn off any recipe fetching and
   * processing.
   */
  onEnabledPrefChange(prefName, oldValue, newValue) {
    if (this._initialized && !newValue) {
      this.uninit();
    } else if (!this._initialized && newValue && this.enabled) {
      // If the feature pref is turned on then turn on recipe processing.
      // If the opt in pref is turned on then turn on recipe processing only if
      // the feature pref is also enabled.
      this.init();
    }
  }

  /**
   * Sets a timer to update recipes every this.intervalInSeconds
   */
  setTimer() {
    // When this function is called, updateRecipes is also called immediately
    timerManager.registerTimer(
      TIMER_NAME,
      () => this.updateRecipes("timer"),
      this.intervalInSeconds
    );
    log.debug("Registered update timer");
  }
}

const RemoteSettingsExperimentLoader = new _RemoteSettingsExperimentLoader();
