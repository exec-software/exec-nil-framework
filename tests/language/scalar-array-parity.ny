struct ArrayRecord
    words: array<text>
    samples: array<f64>
end struct

fn make_words(value: text) -> array<text>
    let mut words: array<text> = ["", value + "-owned"]
    words.push("tail")
    return words
end fn

fn pass_samples(values: array<f64>) -> array<f64>
    return values
end fn

fn adjust_sample(value: f64) -> f64
    let mut result: f64 = value
    result = result + 0.125
    return result
end fn

fn main() -> i32
    let mut samples: array<f64> = [1.25, -2.5, 0.0]
    let mut sample_alias: array<f64> = pass_samples(samples)
    samples[0] = 3.75
    sample_alias.push(6.125)
    println(sample_alias[0] == 3.75)
    println(samples[1] == -2.5)
    println(samples[3] == 6.125)
    println(samples.len())
    let mut empty_samples: array<f64> = []
    println(empty_samples[0] == 0.0)
    empty_samples.push(-0.125)
    println(empty_samples[0] == -0.125)
    samples[-1] = 100.25
    samples[100] = 100.25
    println(samples[0] == 3.75)
    println(samples[-1] == 0.0)
    println(samples[100] == 0.0)

    let mut words: array<text> = make_words("caller")
    let mut alias: array<text> = words
    words[0] = "updated" + "-value"
    alias.push("grown")
    println(alias[0])
    println(words[1])
    println(words[2])
    println(words[3])
    println(words.len())
    words[-1] = "wrong"
    words[99] = "wrong"
    println(words[0])
    println(words[-1] == "")
    println(words[99] == "")
    let mut empty_words: array<text> = []
    println(empty_words[0] == "")
    empty_words.push("")
    println(empty_words[0] == "")

    let record: ArrayRecord = ArrayRecord { words: words, samples: samples }
    println(record.words[1])
    println(record.samples[3] == 6.125)
    let mut count: i32 = 0
    while count < 80
        words.push("resized")
        samples.push(2.25)
        count = count + 1
    end while
    println(record.words.len())
    println(record.words[83])
    println(record.samples[83] == 2.25)
    println(adjust_sample(samples[0]) == 3.875)
    println(adjust_sample(-2.5) == -2.375)
    return 0
end fn
