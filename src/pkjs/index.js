/* global Pebble */
'use strict';

const STORAGE_KEY = 'mario-config';
const CONFIG_URL = 'http://clusterrr.com/pebble_configs/mario_w.php';
const BATTERY_ENDPOINT = 'http://127.0.0.1:1821/battery';
const WEATHER_API_KEY = '5d949dcb47bf776e783aa63ed22d4f60';
const WEATHER_ENDPOINT = 'http://api.openweathermap.org/data/2.5/weather';

let options = {
  config_show_no_phone: true,
  config_show_weather: true,
  config_temperature_units: 0,
  config_show_battery: true,
  config_show_phone_battery: false,
  config_vibe: false,
  config_vibe_hour: false,
  config_background: 0
};

const locationOptions = {
  timeout: 60 * 1000,
  maximumAge: 30 * 60 * 1000
};

const sendMessage = payload => {
  try {
    Pebble.sendAppMessage(payload);
  } catch (error) {
    console.warn(`Pebble.sendAppMessage failed: ${error}`);
  }
};

const kelvinToPreferredUnit = kelvin => {
  const celsius = Math.round(kelvin - 273.15);
  if (options.config_temperature_units === 0) {
    return Math.round(celsius * 1.8 + 32);
  }
  return celsius;
};

const resolveIconId = icon => {
  const base = parseInt(icon.substring(0, 2), 10);
  return icon.charAt(2) === 'n' ? base + 100 : base;
};

const requestWeather = coords => {
  console.log(`Requesting weather for lat=${coords.latitude}, lon=${coords.longitude}`);
  const url = `${WEATHER_ENDPOINT}?lat=${coords.latitude}&lon=${coords.longitude}&APPID=${WEATHER_API_KEY}`;
  const xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.onreadystatechange = () => {
    if (xhr.readyState !== 4) {
      return;
    }
    if (xhr.status !== 200) {
      console.warn(`Weather request failed: HTTP ${xhr.status}`);
      return;
    }
    try {
      const response = JSON.parse(xhr.responseText);
      const weather = response.weather && response.weather[0];
      if (!weather) {
        console.warn('Weather payload missing weather array');
        return;
      }
      const temperature = kelvinToPreferredUnit(response.main.temp);
      const iconId = resolveIconId(weather.icon);
      console.log(`Weather icon=${iconId}, temperature=${temperature}`);
      sendMessage({
        weather_icon_id: iconId,
        weather_temperature: temperature
      });
    } catch (error) {
      console.warn(`Weather JSON parse error: ${error}`);
    }
  };
  xhr.send(null);
};

const requestLocationAndWeather = () => {
  console.log('Requesting location…');
  navigator.geolocation.getCurrentPosition(
    position => requestWeather(position.coords),
    error => console.warn(`Location error: ${error.message || error}`),
    locationOptions
  );
};

const loadStoredOptions = () => {
  const json = window.localStorage.getItem(STORAGE_KEY);
  if (typeof json !== 'string') {
    return;
  }
  try {
    options = JSON.parse(json);
    sendMessage(options);
    console.log(`Loaded stored config: ${json}`);
  } catch (error) {
    console.warn(`Stored config parse error: ${error} — ${json}`);
  }
};

const storeAndApplyOptions = json => {
  window.localStorage.setItem(STORAGE_KEY, json);
  try {
    options = JSON.parse(json);
    sendMessage(options);
    console.log(`Options updated: ${json}`);
    if (options.config_show_weather) {
      setTimeout(requestLocationAndWeather, 5000);
    }
  } catch (error) {
    console.warn(`Response config parse error: ${error} — ${json}`);
  }
};

const openConfiguration = () => {
  const configQuery = encodeURIComponent(JSON.stringify(options));
  let watchInfo = null;
  try {
    watchInfo = Pebble.getActiveWatchInfo();
  } catch (error) {
    console.log(`getActiveWatchInfo error: ${error}`);
  }
  const platform = watchInfo ? watchInfo.platform : 'unknown';
  const url = `${CONFIG_URL}?config=${configQuery}&platform=${platform}&v=4`;
  console.log(`Opening configuration: ${url}`);
  Pebble.openURL(url);
};

const requestBatteryLevel = () => {
  const xhr = new XMLHttpRequest();
  xhr.open('GET', BATTERY_ENDPOINT, true);
  xhr.onreadystatechange = () => {
    if (xhr.readyState !== 4) {
      return;
    }
    if (xhr.status !== 200) {
      console.warn(`Battery request failed: HTTP ${xhr.status}`);
      return;
    }
    try {
      const response = JSON.parse(xhr.responseText);
      const level = Math.round(response.level);
      console.log(`Battery level received: ${level}`);
      sendMessage({ battery_answer: Math.round(level / 10) });
    } catch (error) {
      console.warn(`Battery JSON parse error: ${error}`);
    }
  };
  xhr.send(null);
};

Pebble.addEventListener('ready', () => {
  loadStoredOptions();
});

Pebble.addEventListener('showConfiguration', openConfiguration);

Pebble.addEventListener('webviewclosed', event => {
  const response = decodeURIComponent(event.response || '');
  if (response.startsWith('{') && response.endsWith('}') && response.length > 5) {
    storeAndApplyOptions(response);
  }
});

Pebble.addEventListener('appmessage', event => {
  const payload = event.payload || {};
  console.log(`Received appmessage: ${JSON.stringify(payload)}`);
  if ('weather_request' in payload) {
    setTimeout(requestLocationAndWeather, 1000);
  }
  if ('battery_request' in payload) {
    setTimeout(requestBatteryLevel, 1000);
  }
});
