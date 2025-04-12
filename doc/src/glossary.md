<!-- This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)-->
<!-- Copyright (c) 2023-2025 Doliam-->
<!-- This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).-->
<!-- This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.-->

# Glossary

## Acronyms

| Acronym | Definition                | Comment                                                                                                                    |
|---------|---------------------------|----------------------------------------------------------------------------------------------------------------------------|
| CAD     | Computer Aided Design     |                                                                                                                            |
| ETA     | Estimated Time of Arrival | (from aeronautic jargon) This is the date at which a given event is estimated to occur                                     |
| ETE     | Estimated Time Enroute    | (from aeronautic jargon) This is the remaining time necessary to complete a task                                           |
| LRU     | Least Recently Used       | A classical cache replacement policy where the entry that was least recently used is discarded when a new one is allocated |
| MRO     | Method Research Order     | The inheritance chain from the current class to its most basic base, usually `object`                                      |

## Abbreviations

Some words are so heavily used in this documentation that abbreviating them greatly improve readability.

| Abbreviation | Definition |
|--------------|------------|
| dep          | dependency |
| dir          | directory  |
| repo         | repository |

## Concepts

### Birthday paradox

This is a wellknown counter intuitive problem linked to checksum collision.

It is extensively described [here](https://en.wikipedia.org/wiki/Birthday_problem).

### diamond rule

A feature of python that allows the following behavior:

- A class `D` inherits from `B` and `C` in that order.
- Both `B` and `C` inherit from a class `A`.
- A method `m` is defined on `A` and `C` but not on `B`.
- Then if `m` is called from an instance of `D`, `C.m` will be called and not `B.m` (which turns out to be `A.m`).

It is extensively described [here](https://docs.python.org/3.12/whatsnew/2.2.html#multiple-inheritance-the-diamond-rule).

This feature is a central point that makes python multiple inheritance easy to use and enables the class hierarchy shopping list style.

python computes the MRO in such a way as to enforce the diamond rule.
