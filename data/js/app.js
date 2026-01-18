// Bionic Flower - Additional controls for effects, brightness, and adaptive brightness

// key_color is defined in script.js as const, so we use it directly
var key_effect = "effect";
var key_led_brightness = "led_brightness";
var key_adaptive_brightness = "adaptive_brightness";
var key_weather_debug = "weather_debug";
var key_weather_state = "weather_state";

var id_effect_select = "effect-select";
var id_brightness_slider = "brightness-slider";
var id_brightness_slider_title = "brightness-slider-title";
var id_adaptive_brightness_toggle = "adaptive-brightness-toggle";
var id_color_picker_global = "color-picker-global";
var id_weather_debug_select = "weather-debug-select";

var current_effect = "none";
var current_led_brightness = 100;
var current_adaptive_brightness = true;
var global_selected_color = "#0091DC";
var is_updating_ui = false;
var color_change_pending = false;

function sendGlobalConfiguration() {
    if (is_updating_ui) return;

    var effect = document.getElementById(id_effect_select).value;
    var brightness = Math.round($("#" + id_brightness_slider).slider("getValue") / 10);
    var adaptive = document.getElementById(id_adaptive_brightness_toggle).checked ? 1 : 0;
    var color = document.getElementById(id_color_picker_global).value;

    $.post("/configuration", {
        [key_effect]: effect,
        [key_led_brightness]: brightness,
        [key_adaptive_brightness]: adaptive,
        [key_color]: color
    }, function(response) {
        // Response handled by readResponse in main script
        var data = {};
        String(response).split("&").forEach(function(item) {
            var parts = String(item).split("=");
            data[parts[0]] = parts[1];
        });
        updateGlobalUI(data);
    });
}

function updateGlobalUI(data) {
    is_updating_ui = true;

    // Effect
    var effect = data[key_effect];
    if (effect != null) {
        current_effect = effect;
        document.getElementById(id_effect_select).value = effect;
    }

    // LED Brightness
    var brightness = data[key_led_brightness];
    if (brightness != null) {
        current_led_brightness = Number(brightness);
        $("#" + id_brightness_slider).slider("setValue", current_led_brightness * 10, true);
        document.getElementById(id_brightness_slider_title).textContent = Math.round(current_led_brightness) + "%";
    }

    // Adaptive Brightness
    var adaptive = data[key_adaptive_brightness];
    if (adaptive != null) {
        current_adaptive_brightness = adaptive == "1";
        document.getElementById(id_adaptive_brightness_toggle).checked = current_adaptive_brightness;
    }

    // Color - only update if not currently being changed by user
    var color = data[key_color];
    if (color != null && color.startsWith("#") && !color_change_pending) {
        global_selected_color = color;
        document.getElementById(id_color_picker_global).value = color;
        // Sync hidden color pickers used by script.js
        var hiddenPicker = document.getElementById("color-picker");
        var hiddenPickerManual = document.getElementById("color-picker-manual");
        if (hiddenPicker) hiddenPicker.value = color;
        if (hiddenPickerManual) hiddenPickerManual.value = color;
        // CRITICAL: Also sync script.js's global selected_color variable
        if (typeof selected_color !== 'undefined') {
            selected_color = color;
        }
    }

    // Weather state - update dropdown to show current weather
    var weather = data[key_weather_state];
    if (weather != null && weather != "none") {
        document.getElementById(id_weather_debug_select).value = weather;
    } else {
        document.getElementById(id_weather_debug_select).value = "";
    }

    is_updating_ui = false;
}

// Initialize global controls when DOM is ready
$(document).ready(function() {
    // Initialize brightness slider
    $("#" + id_brightness_slider).slider({});

    // Effect select change
    document.getElementById(id_effect_select).addEventListener("change", function() {
        sendGlobalConfiguration();
    });

    // Brightness slider change
    $("#" + id_brightness_slider).bind("slideStop", function() {
        var val = $("#" + id_brightness_slider).slider("getValue") / 10;
        document.getElementById(id_brightness_slider_title).textContent = Math.round(val) + "%";
        sendGlobalConfiguration();
    });

    // Adaptive brightness toggle change
    document.getElementById(id_adaptive_brightness_toggle).addEventListener("change", function() {
        sendGlobalConfiguration();
    });

    // Color picker - use 'input' event for live updates while picker is open
    var colorPicker = document.getElementById(id_color_picker_global);

    // Block polling updates while color picker is being used
    colorPicker.addEventListener("focus", function() {
        color_change_pending = true;
    });

    // Send color on every change (input event fires while dragging in picker)
    colorPicker.addEventListener("input", function() {
        var newColor = this.value;
        global_selected_color = newColor;
        color_change_pending = true;

        // Sync with script.js
        if (typeof selected_color !== 'undefined') {
            selected_color = newColor;
        }
        var hiddenPicker = document.getElementById("color-picker");
        var hiddenPickerManual = document.getElementById("color-picker-manual");
        if (hiddenPicker) hiddenPicker.value = newColor;
        if (hiddenPickerManual) hiddenPickerManual.value = newColor;

        // Send color to server
        $.post("/configuration", {
            [key_color]: newColor
        });
    });

    // When picker closes, allow polling updates again after short delay
    colorPicker.addEventListener("change", function() {
        var newColor = this.value;
        global_selected_color = newColor;

        // Sync with script.js
        if (typeof selected_color !== 'undefined') {
            selected_color = newColor;
        }
        var hiddenPicker = document.getElementById("color-picker");
        var hiddenPickerManual = document.getElementById("color-picker-manual");
        if (hiddenPicker) hiddenPicker.value = newColor;
        if (hiddenPickerManual) hiddenPickerManual.value = newColor;

        // Final send and re-enable polling after delay
        $.post("/configuration", {
            [key_color]: newColor
        }, function() {
            setTimeout(function() { color_change_pending = false; }, 500);
        });
    });

    // Weather preview change - sends only weather_debug parameter
    document.getElementById(id_weather_debug_select).addEventListener("change", function() {
        var weather = this.value;
        if (weather) {
            $.post("/configuration", {
                [key_weather_debug]: weather
            }, function(response) {
                // Update UI from response
                var data = {};
                String(response).split("&").forEach(function(item) {
                    var parts = String(item).split("=");
                    data[parts[0]] = parts[1];
                });
                updateGlobalUI(data);
            });
        }
    });

    // Extend the existing fetchSensorData response handler
    var originalReadResponse = window.readResponse;
    window.readResponse = function(data) {
        // Parse data first
        var parsed = {};
        String(data).split("&").forEach(function(item) {
            var parts = String(item).split("=");
            parsed[parts[0]] = parts[1];
        });

        // If color change is pending, remove color from data before passing to original handler
        // This prevents the old color from being displayed
        if (color_change_pending) {
            // Rebuild data string without color
            var filteredParts = [];
            String(data).split("&").forEach(function(item) {
                if (!item.startsWith("color=")) {
                    filteredParts.push(item);
                }
            });
            data = filteredParts.join("&");
            delete parsed[key_color];
        }

        // Call original handler if exists
        if (typeof originalReadResponse === 'function') {
            originalReadResponse(data);
        }

        // Update global UI
        updateGlobalUI(parsed);
    };
});
