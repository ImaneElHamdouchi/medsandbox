# Medical Safety Notes

This document explains the medical safety boundaries of MedSandbox.

MedSandbox is not a medical device. It is not intended for diagnosis, treatment, triage, or real patient care.

## Purpose

The clinical part of MedSandbox is an educational demonstration.

It shows how an experimental clinical plugin can be executed inside a constrained Linux environment using:

- resource limits;
- syscall filtering;
- output capture;
- JSON audit logs;
- synthetic FHIR input data.

## Data

The sample data in this repository is synthetic.

It must not be interpreted as real patient data.

The current qSOFA sample contains synthetic observations for:

- respiratory rate;
- systolic blood pressure;
- Glasgow Coma Score.

## qSOFA Plugin

The qSOFA plugin is included as a simple clinical scoring example.

It calculates:

```text
respiratory rate >= 22        -> 1 point
systolic blood pressure <=100 -> 1 point
Glasgow Coma Score < 15       -> 1 point
```

A score of 2 or more is reported as a positive screen requiring clinical review.

This output is not a medical recommendation. It is only a demonstration of how a plugin can read structured clinical data and return a JSON result.

## Not for Patient Care

Do not use this project for:

- diagnosing patients;
- making treatment decisions;
- replacing clinical judgment;
- processing real patient information;
- deploying in a hospital or clinical production environment.

## Safety Assumptions

The clinical examples assume:

- input data is synthetic;
- the plugin is used only for testing;
- results are reviewed as software output, not medical advice;
- the sandbox is educational and not certified for clinical use.

## Future Improvements

Possible future medical-safety improvements:

- add more explicit plugin metadata;
- add validation of required clinical fields;
- add FHIR schema validation;
- add stronger disclaimers in plugin outputs;
- add tests for missing or malformed clinical observations;
- document each clinical score source and interpretation limits.
