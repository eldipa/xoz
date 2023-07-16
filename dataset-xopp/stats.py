import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

df = pd.read_csv('xopp-dataset-2023.csv')
bg_df = pd.read_csv('bg-xopp-dataset-2023.csv')
coords_df = pd.read_csv('coords-xopp-dataset-2023.csv')

# Observations: most of the docs have very few pages (and 1 layer only)
# Only one of the doc examples provided by Xournal have more than 1 layers
sns.histplot(data=bg_df.groupby('sample').count()['page'])
# => hist_of_page_count_per_sample.png


strokes = df[df["type"] == "s"]

# Count how many rows are in each sample (aka strokes count).
# Pick one column that we know will never have nan/nulls so the count
# works as expected (use "type" column here)
# Rename the column so it makes more sense
data = strokes.groupby("sample", as_index=False)['type'].count().rename(columns={'type': 'strokes'})

# Observations: most of the **docs** have between 100 and 10k strokes
sns.histplot(data=data['strokes'], log_scale=(True, False))
# => hist_of_total_count_of_strokes_per_sample.png

data.describe()
#               strokes
#   count     34.000000 <- how many different samples file we have (not useful)
#   mean    6140.676471
#   std    17044.099963
#   min        1.000000 --|
#   25%      246.250000   | Most of the stroke count are ~100 and 3K
#   50%     1913.000000   | with peak at 100K strokes in the whole document
#   75%     3732.000000 --|
#   max    96108.000000


# The same but know counting strokes per sample file and page
data = strokes.groupby(["sample", "page"], as_index=False)['type'].count().rename(columns={'type': 'strokes'})

# Observations: most strokes per page are around 250 strokes but the distribution
# has a long tail from 550 strokes to 2k strokes
sns.histplot(data=data['strokes'])
# => hist_of_count_of_strokes_per_sample_per_page.png


data.describe()
#                  page      strokes
#     count  530.000000   530.000000  <- how many pages we have in total cross all the docs (not useful)
#     mean    44.245283   393.930189
#     std     47.599386   358.503343
#     min      0.000000     1.000000 --| Most of the strokes per page are around 250 strokes per page
#     25%      7.000000   179.250000   | but outliers are around 2k strokes per page
#     50%     26.000000   292.500000   |
#     75%     68.000000   451.000000 --|
#     max    184.000000  2054.000000
#          |___________|
#            not useful


strokes[strokes['widths_cnt'] >= 2].describe()
#                      page     layer    char_length     widths_cnt     coords_cnt
#      count  173175.000000  173175.0  173175.000000  173175.000000  173175.000000  <-- not useful
#      mean       70.999734       0.0    1254.460661      32.363390      64.726779
#      std        58.061678       0.0    1140.925034      31.080085      62.160170
#      min         0.000000       0.0     134.000000       2.000000       4.000000
#      25%        12.000000       0.0     623.000000      15.000000      30.000000
#      50%        62.000000       0.0     956.000000      24.000000      48.000000
#      75%       118.000000       0.0    1546.000000      40.000000      80.000000
#      max       180.000000       0.0   39101.000000    1055.000000    2110.000000
#              |______________________|      |              |               |
#                    not useful              |              \---------------/
#                                            |         strokes are of 4 to 80 coords (of 2 and 40 2D points)
#    text representation of strokes are  ----/         assuming float16, this is between 24 and 480 bytes
#    around 100 and 1500 bytes                         *including* 2D coords *and* 1D width.
#    peak  ~48K bytes                                  peak ~12K bytes
#               |                                       |
#               \---------------------------------------/ estimated reduction of 4 times


strokes[strokes['widths_cnt'] < 2].describe()
#                       page    layer      char_length  widths_cnt    coords_cnt
#        count  35608.000000  35608.0     35608.000000     35608.0  35608.000000  <-- not useful
#        mean      36.441193      0.0       943.989104         0.0     68.058302
#        std       26.864014      0.0      1918.509610         0.0    166.131514
#        min        0.000000      0.0       121.000000         0.0      4.000000
#        25%       12.000000      0.0       259.000000         0.0     14.000000
#        50%       35.000000      0.0       623.000000         0.0     42.000000
#        75%       56.000000      0.0      1273.000000         0.0     92.000000
#        max      184.000000      0.0    144211.000000         0.0  12434.000000
#              |______________________|        |            |                   |
#                    not useful                |            \-------------------/
#                                              |       strokes are of 4 to 80 coords (of 2 and 40 2D points)
#    text representation of strokes are  ------/       assuming float16, this is between 16 and 320 bytes
#    around 100 and 1500 bytes                         2D coords only (no 1D width counted).
#    peak  ~144K bytes                                 peak ~50K bytes
#               |                                       |
#               \---------------------------------------/ estimated reduction of 2.88 times


# Observations: same story for the points except that the tail goes further
sns.histplot(data=strokes, x='coords_cnt', log_scale=(True, False))
# => hist_of_coord_count_across_all_strokes.png

# Observations: the text type are the most common of all the three with lenghts
# from 1 to 1k.
#
# This translates to sizes of 1 to 1K bytes
#
# teximages are a little less frequent but much heavier from 10k to 100k
# images are even a little less frequent but even much heavier from 10k to 1M
#
# These two are 64 encoded so it translates to sizes
#     of 8K to 80K bytes (for teximages)
# and of 8K to 800K bytes (for images)
#
# The numbers were obtained knowning: sz_of_encoded / 1.33 = sz_of_raw
#
# Non-strokes of char length equal to 0 are dropped too. We found only
# 2 text objects with that zero-length
nonstrokes = df[(df["type"] != "s") & (df["char_length"] > 0)]
sns.histplot(data=nonstrokes, x='char_length', hue='type', log_scale=(True, True))
# => hist_of_char_length_by_nonstroke_element_type.png


# Texts only
nonstrokes[nonstrokes['type'] == 't'].describe()
#                 page  layer  char_length  widths_cnt  coords_cnt
#    count  346.000000  346.0   346.000000       346.0       346.0
#    mean    40.037572    0.0    60.942197         0.0         0.0
#    std     35.891806    0.0   146.125862         0.0         0.0
#    min      0.000000    0.0     1.000000         0.0         0.0
#    25%      9.000000    0.0    10.000000         0.0         0.0
#    50%     30.000000    0.0    26.500000         0.0         0.0
#    75%     65.750000    0.0    61.000000         0.0         0.0
#    max    179.000000    0.0  2235.000000         0.0         0.0
#         |__________________|      |             |_______________|
#               not useful          |                 not useful
#                                   |
#                                   \-- most of the texts are less than 80 bytes
#                                       a lot fo them are 26 bytes only
#                                       but peak may be around 2K bytes

# Text count per sample file and page
data = nonstrokes[nonstrokes['type'] == 't'].groupby(["sample", "page"], as_index=False)['type'].count().rename(columns={'type': 'texts'})
data.describe()


nonstrokes[nonstrokes['type'] == 'i'].describe()
#    			  page  layer   char_length  widths_cnt  coords_cnt  fill  capStyle  size    ts
#    count   73.000000   73.0  7.300000e+01        73.0        73.0  73.0       0.0  73.0  73.0
#    mean    17.328767    0.0  2.021699e+05         0.0         0.0   0.0       NaN   0.0   0.0
#    std     25.557155    0.0  2.617348e+05         0.0         0.0   0.0       NaN   0.0   0.0
#    min      1.000000    0.0  1.760000e+03         0.0         0.0   0.0       NaN   0.0   0.0
#    25%      4.000000    0.0  6.798400e+04         0.0         0.0   0.0       NaN   0.0   0.0
#    50%      9.000000    0.0  1.234360e+05         0.0         0.0   0.0       NaN   0.0   0.0
#    75%     17.000000    0.0  1.972560e+05         0.0         0.0   0.0       NaN   0.0   0.0
#    max    141.000000    0.0  1.696420e+06         0.0         0.0   0.0       NaN   0.0   0.0

nonstrokes[nonstrokes['type'] == 'x'].describe()
#   		     page  layer   char_length  widths_cnt  coords_cnt   fill  capStyle   size     ts
#   count  233.000000  233.0    233.000000       233.0       233.0  233.0       0.0  233.0  233.0
#   mean     5.892704    0.0  34980.652361         0.0         0.0    0.0       NaN    0.0    0.0
#   std     10.335809    0.0  15889.636379         0.0         0.0    0.0       NaN    0.0    0.0
#   min      0.000000    0.0   4528.000000         0.0         0.0    0.0       NaN    0.0    0.0
#   25%      0.000000    0.0  21016.000000         0.0         0.0    0.0       NaN    0.0    0.0
#   50%      0.000000    0.0  32852.000000         0.0         0.0    0.0       NaN    0.0    0.0
#   75%     10.000000    0.0  43992.000000         0.0         0.0    0.0       NaN    0.0    0.0
#   max     63.000000    0.0  89468.000000         0.0         0.0    0.0       NaN    0.0    0.0

# Font sizes are quite below 64 and there is little diversity of values
texts = nonstrokes[nonstrokes['type'] == 't']
sns.histplot(data=texts, x='size')
# => hist_of_font_sizes_for_text_elements.png


# Fonts: there are only a few:
sns.histplot(data=texts, x='font')
plt.xticks(rotation=15)
# => hist_of_fonts.png


# Colors?
df['color'].nunique()
# 22

# Fill?
df['fill'].unique()
# array([  0, 128])

# Strokes' Style?
strokes.loc[:, 'style'] = strokes['style'].fillna("default")

# Observation:
# Almost all the strokes use the default style. Only a few use the
# predefined "dot", "dashbot" and "dash" and nobody uses any "custom style"
sns.histplot(data=strokes, x='style', log_scale=(False, True))
# => hist_of_stroke_styles.png


# Audio?
# Nothing: nobody is using the audio (only 1 sample uses and it was
# crafted by me)
strokes[strokes['fn'].notna()]
nonstrokes[nonstrokes['fn'].notna()]


# The source code (tex) of the teximages
#
# Observation: less than 255 but obviously it can be greater
# Mostly in the range of 1 to 50
teximages = nonstrokes[nonstrokes['type'] == 'x']
teximages.loc[:, "text_length"] = teximages['text'].apply(lambda x: len(x))
sns.histplot(data=teximages, x='text_length')
# => hist_of_tex_length_for_teximages.png


teximages.describe()

#                page  layer   char_length  widths_cnt  coords_cnt   fill  capStyle   size     ts  text_length
#   count  233.000000  233.0    233.000000       233.0       233.0  233.0       0.0  233.0  233.0   233.000000  # <-- dont care
#   mean     5.892704    0.0  34980.652361         0.0         0.0    0.0       NaN    0.0    0.0    34.300429
#   std     10.335809    0.0  15889.636379         0.0         0.0    0.0       NaN    0.0    0.0    45.936846
#   min      0.000000    0.0   4528.000000         0.0         0.0    0.0       NaN    0.0    0.0     1.000000
#   25%      0.000000    0.0  21016.000000         0.0         0.0    0.0       NaN    0.0    0.0     9.000000
#   50%      0.000000    0.0  32852.000000         0.0         0.0    0.0       NaN    0.0    0.0    18.000000
#   75%     10.000000    0.0  43992.000000         0.0         0.0    0.0       NaN    0.0    0.0    39.000000
#   max     63.000000    0.0  89468.000000         0.0         0.0    0.0       NaN    0.0    0.0   256.000000
#         |______________________________________________________________________________________|      |
#                                               not useful                                          most are between
#                                                                                                     1 and 50

# Is the background's name used? -> Nop
(bg_df['name'].notna() == True).any()
False

# Observation: by far 'solid' is the most used background, followed by PDF
# The pixmap is just a tiny fraction
sns.histplot(data=bg_df, x='type')
# => hist_of_bg_type_across_all_samples_and_pages.png

# Pixmaps' domains are not very impressive, 5 "absolute", 1 "clone" and 0 "attach"
# PDF's domains are all "absolute"
#
# Let's see the sample file that has this "clone" pixmap background. It looks
# like "clone" is just an alias as "-empty-"
bg_df[bg_df['sample'] == 'et-02']
#     sample    type  page  name config color style    domain           filename  pageno
#  22  et-02  pixmap     0   NaN    NaN   NaN   NaN  absolute  /home../paper.png       0
#  23  et-02  pixmap     1   NaN    NaN   NaN   NaN     clone                  0       0
#  24  et-02  pixmap     2   NaN    NaN   NaN   NaN  absolute            -empty-       0
#  25  et-02  pixmap     3   NaN    NaN   NaN   NaN  absolute            -empty-       0
#  26  et-02  pixmap     4   NaN    NaN   NaN   NaN  absolute            -empty-       0
#  27  et-02  pixmap     5   NaN    NaN   NaN   NaN  absolute            -empty-       0

# A lot of backgrounds fall under ~50 / ~80 pageno, but then there is a consistent
# uniform pageno distribution up to ~350 pageno
sns.histplot(data=bg_df[(bg_df['type'] == 'pdf') & (bg_df['pageno'] > 1)], x='pageno')
# => hist_of_pageno_of_pdfs_bg.png

# Do we have gaps between PDF pages ?
#
# We calculate the difference between the max and min pageno and we divide it
# by the page count.
# We should get
#   - 1 when there is no gap
#   - < 0 when there are more pages than pageno (backed by a PDF)
#   - > 0 when there are gaps between pagenos
# (this is only an estimation)
grp = pdfs.groupby('sample', as_index=False)
selected_samples = list(grp['pageno'].max()['sample'])
page_cnt = bg_df.groupby('sample').count().loc[selected_samples]['page']
(grp['pageno'].max()['pageno'] - grp['pageno'].min()['pageno'] + 1) / list(page_cnt)
#   0    1.000000
#   1    1.000000
#   2    1.000000
#   3    1.000000
#   4    1.000000
#   5    0.832558       <---- this is uk-02 "the algebra file"
#   Name: pageno, dtype: float64

# Do we have difference between width / height within the same sample file? -> Yes and No
#
# Group by sample, select with / height and calc the number of unique
# within the sample, then
(bg_df.groupby('sample')['width'].nunique() != 1).any()
# False -> all the papes in each sample have the same width
(bg_df.groupby('sample')['height'].nunique() != 1).any()
# True -> *almost* all the papes in each sample have the same height

# The "outlier" is the sample file in-14
bg_df[bg_df['sample'] == 'in-14']['height'].describe()
#   count     98.000000
#   mean     311.372133
#   std        1.880791
#   min      303.302200
#   25%      311.806000
#   50%      311.806000
#   75%      311.806000
#   max      311.806000
#   Name: height, dtype: float64

# Width/height in the range of 100 to 1000. No standard ratio: some samples
# have width >> height (wide); some width << height (tall).
sns.histplot(data=bg_df.groupby('sample').max(), x='width', y='height')
# => hist_of_page_witdth_and_height_per_sample.png

# How are distributed the x, y, w coords of each stroke in each layer / sample file?
#
# x and y coords have a small std for most of the cases but there are a significant
# amount of cases where the std is as larger as the mean
#
# In some rare cases there are x/y coords with std == 0 (probably due straight
# horizontal / vertical lines)
#
# p coords have a very small std, less than 1, mostly concentrated around std of 0.2
# with most of the values less than 0.6.
#
# The mean is also between 0 and 2, with a heavy concentration around 0.67
#
# There are a lot of p coords with mean == std == 0.0 for the strokes that have no pressure values
#
descr = coords_df.groupby(['sample', 'layer', 'stroke'], as_index=False)['x', 'y', 'p'].describe()

sns.histplot(data=descr['x'], x='std', y='mean')
sns.histplot(data=descr['y'], x='std', y='mean')
sns.histplot(data=descr['p'], x='std', y='mean')
# => hist_x_coord_mean_std.png hist_y_coord_mean_std.png hist_p_coord_mean_std.png

# The widths are mostly concentrated around 1 and 1.5; they expand to the 0 - 5 range
# but there are outliers up to 20
# The std is also mostly less than 0.25 but it can reach up to 1.75
#
# They are *totally* independent of the pressure values!
descr2 = coords_df.groupby(['sample', 'layer'], as_index=False)['width'].describe()
sns.histplot(data=descr2['width'], x='std', y='mean')
# => hist_w_coord_mean_std.png

# Are related the x y ? Meah...
sns.histplot(data=coords_df, x='x', y='y')
# => hist_x_y_coords.png


