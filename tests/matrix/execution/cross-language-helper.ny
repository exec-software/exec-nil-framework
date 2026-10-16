# 14-cross-language-helper.ny  --  page 2 of 3
#
# The Ny half. This is ordinary Ny source with no Matrix awareness: it declares
# a module, imports another module, derives from a class and constructs a
# record. Nothing here says which surface declared those types, because by the
# time Ny sees them they are ordinary NIL records and classes.
#
# It is deliberately .ny rather than .matrix. That is the proof obligation:
# three pages, two surfaces, one resolved program, and neither front end needs
# to parse the other's syntax.

module tour.ny_helper

import tour.mixed

# An Ny paragraph that the third page will call from Matrix.
pub fn scale(input: i64) -> i64
    return input * 2
end fn

# Ny DERIVING FROM A MATRIX CLASS.
#
# tour.mixed::Sampler was declared in 14-cross-language.matrix. Its field and
# its virtual method are reached here exactly as if an Ny class had declared
# them, and the override participates in the same virtual dispatch.
pub class OffsetSampler inherits tour.mixed::Sampler
    bonus: i64
    pub override fn sample(self, raw: i64) -> i64
        return raw + self.offset + self.bonus
    end fn
end class

# Ny CONSTRUCTING A MATRIX RECORD, and returning it across the boundary.
pub fn describe(reading: i64) -> tour.mixed::Measurement
    return tour.mixed::Measurement { sensor: "secondary", reading: reading }
end fn
