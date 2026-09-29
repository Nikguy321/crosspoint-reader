#include "BuiltInQuotes.h"

namespace sleepcards::quote {
namespace {

// Wording, spelling and punctuation as in the Gutenberg text (an excerpt may start with a capital
// and end with a full stop where the sentence runs on). Source beside each entry.
constexpr BuiltInQuote QUOTES[] = {
    // https://www.gutenberg.org/ebooks/205 (Walden, 1854)
    {"We need the tonic of wildness.", "Henry David Thoreau, Walden"},
    {"Heaven is under our feet as well as over our heads.", "Henry David Thoreau, Walden"},
    {"Time is but the stream I go a-fishing in. I drink at it; but while I drink I see the sandy bottom and detect "
     "how shallow it is. Its thin current slides away, but eternity remains. I would drink deeper; fish in the sky, "
     "whose bottom is pebbly with stars.",
     "Henry David Thoreau, Walden"},
    {"I went to the woods because I wished to live deliberately, to front only the essential facts of life, and see "
     "if I could not learn what it had to teach, and not, when I came to die, discover that I had not lived.",
     "Henry David Thoreau, Walden"},
    {"How many a man has dated a new era in his life from the reading of a book.", "Henry David Thoreau, Walden"},
    {"Books are the treasured wealth of the world and the fit inheritance of generations and nations.",
     "Henry David Thoreau, Walden"},
    {"A lake is the landscape\xE2\x80\x99s most beautiful and expressive feature. It is earth\xE2\x80\x99s eye; "
     "looking into which the beholder measures the depth of his own nature.",
     "Henry David Thoreau, Walden"},
    // https://www.gutenberg.org/ebooks/1022 (Walking, 1862)
    {"In Wildness is the preservation of the World.", "Henry David Thoreau, Walking"},
    // https://www.gutenberg.org/ebooks/60929 (Our National Parks, 1901)
    {"Climb the mountains and get their good tidings. Nature\xE2\x80\x99s peace will flow into you as sunshine "
     "flows into trees.",
     "John Muir, Our National Parks"},
    // https://www.gutenberg.org/ebooks/32540 (My First Summer in the Sierra, 1911)
    {"Another glorious Sierra day in which one seems to be dissolved and absorbed and sent pulsing onward we know "
     "not where. Life seems neither long nor short, and we take no more heed to save time or make haste than do the "
     "trees and stars.",
     "John Muir, My First Summer in the Sierra"},
    // https://www.gutenberg.org/ebooks/326 (Steep Trails, 1918)
    {"In every walk with Nature one receives far more than he seeks.", "John Muir, Steep Trails"},
    // https://www.gutenberg.org/ebooks/58611 (New Hampshire, 1923)
    {"The woods are lovely, dark and deep,\nBut I have promises to keep,\nAnd miles to go before I sleep.",
     "Robert Frost, Stopping by Woods on a Snowy Evening"},
    // https://www.gutenberg.org/ebooks/29345 (Mountain Interval, 1916)
    {"Two roads diverged in a wood, and I\xE2\x80\x94\nI took the one less traveled by,\nAnd that has made all the "
     "difference.",
     "Robert Frost, The Road Not Taken"},
    // https://www.gutenberg.org/ebooks/535 (Travels with a Donkey in the Cevennes, 1879)
    {"I travel not to go anywhere, but to go. I travel for travel's sake. The great affair is to move.",
     "Robert Louis Stevenson, Travels with a Donkey in the Cevennes"},
    {"There is one stirring hour unknown to those who dwell in houses, when a wakeful influence goes abroad over the "
     "sleeping hemisphere, and all the outdoor world are on their feet.",
     "Robert Louis Stevenson, Travels with a Donkey in the Cevennes"},
    // https://www.gutenberg.org/ebooks/516 (The Silverado Squatters, 1883)
    {"There is no foreign land; it is the traveller only that is foreign.",
     "Robert Louis Stevenson, The Silverado Squatters"},
    // https://www.gutenberg.org/ebooks/487 (Songs of Travel, 1896: "The Vagabond")
    {"Give to me the life I love,\nLet the lave go by me,\nGive the jolly heaven above\nAnd the byway nigh me.\nBed "
     "in the bush with stars to see,\nBread I dip in the river\xE2\x80\x94\nThere\xE2\x80\x99s the life for a man "
     "like me,\nThere\xE2\x80\x99s the life for ever.",
     "Robert Louis Stevenson, The Vagabond"},
    // https://www.gutenberg.org/ebooks/1322 (Leaves of Grass)
    {"Now I see the secret of the making of the best persons,\nIt is to grow in the open air and to eat and sleep "
     "with the earth.",
     "Walt Whitman, Song of the Open Road"},
    {"Afoot and light-hearted I take to the open road,\nHealthy, free, the world before me,\nThe long brown path "
     "before me leading wherever I choose.",
     "Walt Whitman, Song of the Open Road"},
    {"Till rising and gliding out I wander\xE2\x80\x99"
     "d off by myself,\nIn the mystical moist night-air, and from time to time,\nLook\xE2\x80\x99"
     "d up in perfect silence at the stars.",
     "Walt Whitman, When I Heard the Learn\xE2\x80\x99"
     "d Astronomer"},
    // https://www.gutenberg.org/ebooks/29433 (Nature, 1836)
    {"Nature always wears the colors of the spirit.", "Ralph Waldo Emerson, Nature"},
    {"If the stars should appear one night in a thousand years, how would men believe and adore; and preserve for "
     "many generations the remembrance of the city of God which had been shown!",
     "Ralph Waldo Emerson, Nature"},
    // https://www.gutenberg.org/ebooks/8209 (Poems, 1817)
    {"The poetry of earth is never dead.", "John Keats, On the Grasshopper and Cricket"},
    // https://www.gutenberg.org/ebooks/12242 (Poems, Three Series)
    {"There is no frigate like a book\nTo take us lands away,\nNor any coursers like a page\nOf prancing poetry.",
     "Emily Dickinson, A Book"},
    // https://www.gutenberg.org/ebooks/5131 (Childe Harold's Pilgrimage, Canto IV)
    {"There is a pleasure in the pathless woods,\nThere is a rapture on the lonely shore,\nThere is society where "
     "none intrudes,\nBy the deep Sea, and music in its roar:\nI love not Man the less, but Nature more.",
     "Lord Byron, Childe Harold\xE2\x80\x99s Pilgrimage"},
    // https://www.gutenberg.org/ebooks/8601 (The Early Poems of Alfred Lord Tennyson)
    {"One equal temper of heroic hearts,\nMade weak by time and fate, but strong in will\nTo strive, to seek, to "
     "find, and not to yield.",
     "Alfred Tennyson, Ulysses"},
    // https://www.gutenberg.org/ebooks/1365 (The Complete Poetical Works of Henry Wadsworth Longfellow)
    {"And the night shall be filled with music\nAnd the cares, that infest the day,\nShall fold their tents, like "
     "the Arabs,\nAnd as silently steal away.",
     "Henry Wadsworth Longfellow, The Day Is Done"},
    // https://www.gutenberg.org/ebooks/1540 (The Tempest)
    {"We are such stuff\nAs dreams are made on, and our little life\nIs rounded with a sleep.",
     "William Shakespeare, The Tempest"},
    // https://www.gutenberg.org/ebooks/1533 (Macbeth)
    {"Sleep that knits up the ravell\xE2\x80\x99"
     "d sleave of care,\nThe death of each day\xE2\x80\x99s life, sore labour\xE2\x80\x99s bath,\nBalm of hurt "
     "minds, great nature\xE2\x80\x99s second course,\nChief nourisher in life\xE2\x80\x99s feast.",
     "William Shakespeare, Macbeth"},
    // https://www.gutenberg.org/ebooks/1528 (Troilus and Cressida)
    {"One touch of nature makes the whole world kin.", "William Shakespeare, Troilus and Cressida"},
    // https://www.gutenberg.org/ebooks/575 (The Essays or Counsels, Civil and Moral)
    {"Reading maketh a full man; conference a ready man; and writing an exact man.", "Francis Bacon, Of Studies"},
    // https://www.gutenberg.org/ebooks/121 (Northanger Abbey, 1817)
    {"The person, be it gentleman or lady, who has not pleasure in a good novel, must be intolerably stupid.",
     "Jane Austen, Northanger Abbey"},
    // https://www.gutenberg.org/ebooks/2701 (Moby-Dick, 1851)
    {"It is not down in any map; true places never are.", "Herman Melville, Moby-Dick"},
    // https://www.gutenberg.org/ebooks/3176 (The Innocents Abroad, 1869)
    {"Travel is fatal to prejudice, bigotry and narrow-mindedness.", "Mark Twain, The Innocents Abroad"},
    // https://www.gutenberg.org/ebooks/289 (The Wind in the Willows, 1908)
    {"Believe me, my young friend, there is nothing\xE2\x80\x94"
     "absolute nothing\xE2\x80\x94half so much worth doing as simply messing about in boats.",
     "Kenneth Grahame, The Wind in the Willows"},
    // https://www.gutenberg.org/ebooks/43855 (The Way to Wealth, 1758)
    {"Early to bed, and early to rise, makes a man healthy, wealthy, and wise.",
     "Benjamin Franklin, The Way to Wealth"},
    // https://www.gutenberg.org/ebooks/58821 (The Strenuous Life, 1899)
    {"Far better it is to dare mighty things, to win glorious triumphs, even though checkered by failure, than to "
     "take rank with those poor spirits who neither enjoy much nor suffer much, because they live in the gray "
     "twilight that knows not victory nor defeat.",
     "Theodore Roosevelt, The Strenuous Life"},
};

}  // namespace

size_t builtInCount() { return sizeof(QUOTES) / sizeof(QUOTES[0]); }

const BuiltInQuote* builtInQuote(const size_t index) { return index < builtInCount() ? &QUOTES[index] : nullptr; }

}  // namespace sleepcards::quote
