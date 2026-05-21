def calculate_gain(rf, rg) {
    return 1.0 + (rf / rg);
}

gain = calculate_gain(10000.0, 1000.0);
viora_flux_print("Calculated Op-Amp Gain:");
flux_print_num(gain);

# Test new bridge functions
msg = flux_concat("Using bridge: Gain is ", flux_to_str(gain));
viora_flux_print(msg);
