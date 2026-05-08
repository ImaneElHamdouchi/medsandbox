#include <stdio.h>
#include <string.h>
#include <jansson.h>

static int has_loinc(json_t *resource, const char *loinc) {
    json_t *coding = json_object_get(json_object_get(resource, "code"), "coding");
    if (!json_is_array(coding)) return 0;

    size_t i;
    json_t *item;

    json_array_foreach(coding, i, item) {
        json_t *code = json_object_get(item, "code");
        if (json_is_string(code) && strcmp(json_string_value(code), loinc) == 0) {
            return 1;
        }
    }

    return 0;
}

static int get_value(json_t *resource, double *out) {
    json_t *value = json_object_get(json_object_get(resource, "valueQuantity"), "value");

    if (json_is_number(value)) {
        *out = json_number_value(value);
        return 1;
    }

    value = json_object_get(resource, "valueInteger");

    if (json_is_integer(value)) {
        *out = (double)json_integer_value(value);
        return 1;
    }

    return 0;
}

static int find_observation(json_t *bundle, const char *loinc, double *out) {
    json_t *entries = json_object_get(bundle, "entry");
    if (!json_is_array(entries)) return 0;

    size_t i;
    json_t *entry;

    json_array_foreach(entries, i, entry) {
        json_t *resource = json_object_get(entry, "resource");
        json_t *type = json_object_get(resource, "resourceType");

        if (!json_is_string(type)) continue;
        if (strcmp(json_string_value(type), "Observation") != 0) continue;

        if (has_loinc(resource, loinc) && get_value(resource, out)) {
            return 1;
        }
    }

    return 0;
}

static int print_error(const char *message) {
    json_t *out = json_object();

    json_object_set_new(out, "score_name", json_string("qSOFA"));
    json_object_set_new(out, "valid", json_false());
    json_object_set_new(out, "error", json_string(message));
    json_object_set_new(out, "disclaimer", json_string("synthetic data only, not for patient care"));

    json_dumpf(out, stdout, JSON_INDENT(2));
    printf("\n");
    json_decref(out);

    return 2;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return print_error("usage: qsofa <patient_fhir_bundle.json>");
    }

    json_error_t error;
    json_t *bundle = json_load_file(argv[1], 0, &error);

    if (bundle == NULL) {
        return print_error(error.text);
    }

    double rr = 0.0;
    double sbp = 0.0;
    double gcs = 0.0;

    if (!find_observation(bundle, "9279-1", &rr) ||
        !find_observation(bundle, "8480-6", &sbp) ||
        !find_observation(bundle, "9269-2", &gcs)) {
        json_decref(bundle);
        return print_error("missing required qSOFA observations");
    }

    int score = 0;
    int rr_point = rr >= 22.0;
    int sbp_point = sbp <= 100.0;
    int gcs_point = gcs < 15.0;

    score = rr_point + sbp_point + gcs_point;

    json_t *out = json_object();
    json_t *inputs = json_object();
    json_t *components = json_object();

    json_object_set_new(inputs, "respiratory_rate", json_real(rr));
    json_object_set_new(inputs, "systolic_bp", json_real(sbp));
    json_object_set_new(inputs, "glasgow_coma_score", json_real(gcs));

    json_object_set_new(components, "respiratory_rate_ge_22", json_boolean(rr_point));
    json_object_set_new(components, "systolic_bp_le_100", json_boolean(sbp_point));
    json_object_set_new(components, "gcs_lt_15", json_boolean(gcs_point));

    json_object_set_new(out, "score_name", json_string("qSOFA"));
    json_object_set_new(out, "valid", json_true());
    json_object_set_new(out, "score", json_integer(score));
    json_object_set_new(out, "positive_screen", json_boolean(score >= 2));
    json_object_set_new(out, "interpretation", json_string(score >= 2 ? "requires clinical review" : "negative screen"));
    json_object_set_new(out, "inputs", inputs);
    json_object_set_new(out, "components", components);
    json_object_set_new(out, "disclaimer", json_string("synthetic data only, not for patient care"));

    json_dumpf(out, stdout, JSON_INDENT(2));
    printf("\n");

    json_decref(out);
    json_decref(bundle);

    return 0;
}
