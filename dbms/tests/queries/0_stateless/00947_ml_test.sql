CREATE DATABASE IF NOT EXISTS test;
DROP TABLE IF EXISTS test.defaults;
CREATE TABLE IF NOT EXISTS test.defaults
(
    param1 Float64,
    param2 Float64,
    target Float64,
    predict1 Float64,
    predict2 Float64
) ENGINE = Memory;
insert into test.defaults values (-3.273, -1.452, 4.267, 20.0, 40.0), (0.121, -0.615, 4.290, 20.0, 40.0), (-1.099, 2.755, -3.060, 20.0, 40.0), (1.090, 2.945, -2.346, 20.0, 40.0), (0.305, 2.179, -1.205, 20.0, 40.0), (-0.925, 0.702, 1.134, 20.0, 40.0), (3.178, -1.316, 7.221, 20.0, 40.0), (-2.756, -0.473, 2.569, 20.0, 40.0), (3.665, 2.303, 0.226, 20.0, 40.0), (1.662, 1.951, -0.070, 20.0, 40.0), (2.869, 0.593, 3.249, 20.0, 40.0), (0.818, -0.593, 4.594, 20.0, 40.0), (-1.917, 0.916, 0.209, 20.0, 40.0), (2.706, 1.523, 1.307, 20.0, 40.0), (0.219, 2.162, -1.214, 20.0, 40.0), (-4.510, 1.376, -2.007, 20.0, 40.0), (4.284, -0.515, 6.173, 20.0, 40.0), (-1.101, 2.810, -3.170, 20.0, 40.0), (-1.810, -1.117, 4.329, 20.0, 40.0), (0.055, 1.115, 0.797, 20.0, 40.0), (-2.178, 2.904, -3.898, 20.0, 40.0), (-3.494, -1.814, 4.882, 20.0, 40.0), (3.027, 0.476, 3.562, 20.0, 40.0), (-1.434, 1.151, -0.018, 20.0, 40.0), (1.180, 0.992, 1.606, 20.0, 40.0), (0.015, 0.971, 1.067, 20.0, 40.0), (-0.511, -0.875, 4.495, 20.0, 40.0), (0.961, 2.348, -1.216, 20.0, 40.0), (-2.279, 0.038, 1.785, 20.0, 40.0), (-1.568, -0.248, 2.712, 20.0, 40.0), (-0.496, 0.366, 2.020, 20.0, 40.0), (1.177, -1.401, 6.390, 20.0, 40.0), (2.882, -1.442, 7.325, 20.0, 40.0), (-1.066, 1.817, -1.167, 20.0, 40.0), (-2.144, 2.791, -3.655, 20.0, 40.0), (-4.370, 2.228, -3.642, 20.0, 40.0), (3.996, 2.775, -0.553, 20.0, 40.0), (0.289, 2.055, -0.965, 20.0, 40.0), (-0.588, -1.601, 5.908, 20.0, 40.0), (-1.801, 0.417, 1.265, 20.0, 40.0), (4.375, -1.499, 8.186, 20.0, 40.0), (-2.618, 0.038, 1.615, 20.0, 40.0), (3.616, -0.833, 6.475, 20.0, 40.0), (-4.045, -1.558, 4.094, 20.0, 40.0), (-3.962, 0.636, -0.253, 20.0, 40.0), (3.505, 2.625, -0.497, 20.0, 40.0), (3.029, -0.523, 5.560, 20.0, 40.0), (-3.520, -0.474, 2.188, 20.0, 40.0), (2.430, -1.469, 7.154, 20.0, 40.0), (1.547, -1.654, 7.082, 20.0, 40.0), (-1.370, 0.575, 1.165, 20.0, 40.0), (-1.869, -1.555, 5.176, 20.0, 40.0), (3.536, 2.841, -0.913, 20.0, 40.0), (-3.810, 1.220, -1.344, 20.0, 40.0), (-1.971, 1.462, -0.910, 20.0, 40.0), (-0.243, 0.167, 2.545, 20.0, 40.0), (-1.403, 2.645, -2.991, 20.0, 40.0), (0.532, -0.114, 3.494, 20.0, 40.0), (-1.678, 0.975, 0.212, 20.0, 40.0), (-0.656, 2.140, -1.609, 20.0, 40.0), (1.743, 2.631, -1.390, 20.0, 40.0), (2.586, 2.943, -1.593, 20.0, 40.0), (-0.512, 2.969, -3.195, 20.0, 40.0), (2.283, -0.100, 4.342, 20.0, 40.0), (-4.293, 0.872, -0.890, 20.0, 40.0), (3.411, 1.300, 2.106, 20.0, 40.0), (-0.281, 2.951, -3.042, 20.0, 40.0), (-4.442, 0.384, 0.012, 20.0, 40.0), (1.194, 1.746, 0.104, 20.0, 40.0), (-1.152, 1.862, -1.300, 20.0, 40.0), (1.362, -1.341, 6.363, 20.0, 40.0), (-4.488, 2.618, -4.481, 20.0, 40.0), (3.419, -0.564, 5.837, 20.0, 40.0), (-3.392, 0.396, 0.512, 20.0, 40.0), (-1.629, -0.909, 4.003, 20.0, 40.0), (4.447, -1.088, 7.399, 20.0, 40.0), (-1.232, 1.699, -1.014, 20.0, 40.0), (-1.286, -0.609, 3.575, 20.0, 40.0), (2.437, 2.796, -1.374, 20.0, 40.0), (-4.864, 1.989, -3.410, 20.0, 40.0), (-1.716, -1.399, 4.940, 20.0, 40.0), (-3.084, 1.858, -2.259, 20.0, 40.0), (2.828, -0.319, 5.053, 20.0, 40.0), (-1.226, 2.586, -2.786, 20.0, 40.0), (2.456, 0.092, 4.044, 20.0, 40.0), (-0.989, 2.375, -2.245, 20.0, 40.0), (3.268, 0.935, 2.765, 20.0, 40.0), (-4.128, -1.995, 4.927, 20.0, 40.0), (-1.083, 2.197, -1.935, 20.0, 40.0), (-3.471, -1.198, 3.660, 20.0, 40.0), (4.617, -1.136, 7.579, 20.0, 40.0), (2.054, -1.675, 7.378, 20.0, 40.0), (4.106, 2.326, 0.402, 20.0, 40.0), (1.558, 0.310, 3.158, 20.0, 40.0), (0.792, 0.900, 1.596, 20.0, 40.0), (-3.229, 0.300, 0.785, 20.0, 40.0), (3.787, -0.793, 6.479, 20.0, 40.0), (1.786, 2.288, -0.684, 20.0, 40.0), (2.643, 0.223, 3.875, 20.0, 40.0), (-3.592, 2.122, -3.040, 20.0, 40.0), (4.519, -1.760, 8.779, 20.0, 40.0), (3.221, 2.255, 0.101, 20.0, 40.0), (4.151, 1.788, 1.500, 20.0, 40.0), (-1.033, -1.195, 4.874, 20.0, 40.0), (-1.636, -1.037, 4.257, 20.0, 40.0), (-3.548, 1.911, -2.596, 20.0, 40.0), (4.829, -0.293, 6.001, 20.0, 40.0), (-4.684, -1.664, 3.986, 20.0, 40.0), (4.531, -0.503, 6.271, 20.0, 40.0), (-3.503, -1.606, 4.460, 20.0, 40.0), (-2.036, -1.522, 5.027, 20.0, 40.0), (-0.473, -0.617, 3.997, 20.0, 40.0), (-1.554, -1.630, 5.483, 20.0, 40.0), (-3.567, -1.043, 3.302, 20.0, 40.0), (-2.038, 0.579, 0.823, 20.0, 40.0), (-3.040, 0.857, -0.233, 20.0, 40.0), (4.610, 0.562, 4.181, 20.0, 40.0), (-3.323, -1.938, 5.215, 20.0, 40.0), (4.314, 1.720, 1.717, 20.0, 40.0), (-1.220, 0.615, 1.161, 20.0, 40.0), (-2.556, 1.120, -0.519, 20.0, 40.0), (-3.717, -0.108, 1.358, 20.0, 40.0), (4.689, -1.826, 8.996, 20.0, 40.0), (3.452, 0.506, 3.713, 20.0, 40.0), (2.472, 0.612, 3.012, 20.0, 40.0), (3.452, 0.450, 3.826, 20.0, 40.0), (1.207, 2.585, -1.567, 20.0, 40.0), (-4.826, 1.090, -1.593, 20.0, 40.0), (3.116, -1.118, 6.794, 20.0, 40.0), (0.448, 2.732, -2.240, 20.0, 40.0), (-1.096, -0.525, 3.503, 20.0, 40.0), (-4.680, -0.238, 1.137, 20.0, 40.0), (2.552, -1.403, 7.082, 20.0, 40.0), (0.719, 2.997, -2.635, 20.0, 40.0), (0.347, -1.966, 7.105, 20.0, 40.0), (2.958, -0.404, 5.288, 20.0, 40.0), (0.722, -1.950, 7.261, 20.0, 40.0), (-2.851, -0.986, 3.546, 20.0, 40.0), (-4.316, -0.439, 1.721, 20.0, 40.0), (-1.685, -0.201, 2.560, 20.0, 40.0), (1.856, 0.190, 3.549, 20.0, 40.0), (-2.052, 0.206, 1.562, 20.0, 40.0), (-2.504, -0.646, 3.041, 20.0, 40.0), (3.235, 0.882, 2.854, 20.0, 40.0), (-1.366, -1.573, 5.463, 20.0, 40.0), (-3.447, 2.419, -3.562, 20.0, 40.0), (4.155, 2.092, 0.893, 20.0, 40.0), (-0.935, 0.209, 2.116, 20.0, 40.0), (3.117, -1.821, 8.201, 20.0, 40.0), (3.759, 0.577, 3.725, 20.0, 40.0), (-0.938, 2.992, -3.453, 20.0, 40.0), (-0.525, 2.341, -1.945, 20.0, 40.0), (4.540, 2.625, 0.019, 20.0, 40.0), (-2.097, 1.190, -0.429, 20.0, 40.0), (-2.672, 1.983, -2.302, 20.0, 40.0), (-3.038, -1.490, 4.460, 20.0, 40.0), (-0.943, 2.149, -1.770, 20.0, 40.0), (0.739, 1.598, 0.174, 20.0, 40.0), (1.828, 1.853, 0.208, 20.0, 40.0), (4.856, 0.137, 5.153, 20.0, 40.0), (-1.617, 0.468, 1.255, 20.0, 40.0), (-1.972, 2.053, -2.092, 20.0, 40.0), (-4.633, 1.389, -2.094, 20.0, 40.0), (-3.628, -1.156, 3.498, 20.0, 40.0), (3.597, 1.034, 2.731, 20.0, 40.0), (-1.488, -0.002, 2.261, 20.0, 40.0), (0.749, 1.921, -0.468, 20.0, 40.0), (1.304, -1.371, 6.394, 20.0, 40.0), (4.587, 2.936, -0.579, 20.0, 40.0), (-2.241, 1.791, -1.703, 20.0, 40.0), (-2.945, 1.372, -1.216, 20.0, 40.0), (1.375, 0.395, 2.898, 20.0, 40.0), (-1.281, -0.641, 3.642, 20.0, 40.0), (2.178, 0.895, 2.299, 20.0, 40.0), (3.031, -0.786, 6.087, 20.0, 40.0), (-1.385, -0.375, 3.058, 20.0, 40.0), (4.041, -0.431, 5.882, 20.0, 40.0), (0.480, -0.507, 4.254, 20.0, 40.0), (-3.797, 0.140, 0.822, 20.0, 40.0), (2.355, 2.502, -0.827, 20.0, 40.0), (1.376, -1.583, 6.854, 20.0, 40.0), (0.164, 1.405, 0.273, 20.0, 40.0), (-1.273, 1.471, -0.579, 20.0, 40.0), (0.770, 2.246, -1.107, 20.0, 40.0), (4.552, 2.904, -0.533, 20.0, 40.0), (4.259, -1.772, 8.674, 20.0, 40.0), (-0.309, 1.159, 0.528, 20.0, 40.0), (3.581, 2.700, -0.610, 20.0, 40.0), (-3.202, 0.346, 0.707, 20.0, 40.0), (-1.575, 1.242, -0.271, 20.0, 40.0), (-1.584, -0.493, 3.194, 20.0, 40.0), (-3.778, 0.150, 0.810, 20.0, 40.0), (-4.675, 1.749, -2.835, 20.0, 40.0), (3.567, -0.792, 6.367, 20.0, 40.0), (-0.417, 1.399, -0.006, 20.0, 40.0), (-4.672, 2.007, -3.349, 20.0, 40.0), (-1.034, 0.196, 2.090, 20.0, 40.0), (-3.796, 2.496, -3.890, 20.0, 40.0), (3.532, -0.497, 5.759, 20.0, 40.0), (4.868, -1.359, 8.151, 20.0, 40.0), (-0.769, 0.302, 2.011, 20.0, 40.0), (4.475, 2.612, 0.014, 20.0, 40.0), (-3.532, -0.395, 2.024, 20.0, 40.0), (0.322, 0.675, 1.812, 20.0, 40.0), (-2.028, -1.942, 5.870, 20.0, 40.0), (1.810, -1.244, 6.392, 20.0, 40.0), (-0.783, 1.242, 0.124, 20.0, 40.0), (-4.745, -1.300, 3.227, 20.0, 40.0), (1.902, 1.973, 0.005, 20.0, 40.0), (-3.453, -1.429, 4.132, 20.0, 40.0), (1.559, 0.986, 1.808, 20.0, 40.0), (0.128, 2.754, -2.443, 20.0, 40.0), (2.759, 1.727, 0.926, 20.0, 40.0), (-4.468, 1.690, -2.614, 20.0, 40.0), (-2.368, -1.922, 5.659, 20.0, 40.0), (-2.766, 2.128, -2.640, 20.0, 40.0), (0.967, -1.825, 7.133, 20.0, 40.0), (-2.854, 2.855, -4.136, 20.0, 40.0), (-2.944, 1.875, -2.222, 20.0, 40.0), (-2.632, -0.983, 3.649, 20.0, 40.0), (2.427, 2.239, -0.266, 20.0, 40.0), (-1.726, -0.838, 3.812, 20.0, 40.0), (0.007, -0.903, 4.809, 20.0, 40.0), (-2.013, 1.092, -0.191, 20.0, 40.0), (-0.449, 0.970, 0.836, 20.0, 40.0), (1.396, 0.411, 2.876, 20.0, 40.0), (-1.115, -1.790, 6.023, 20.0, 40.0), (3.748, 1.917, 1.039, 20.0, 40.0), (2.978, 1.043, 2.404, 20.0, 40.0), (-3.969, 2.514, -4.013, 20.0, 40.0), (4.455, -0.050, 5.328, 20.0, 40.0), (-3.065, -0.846, 3.160, 20.0, 40.0), (-1.069, 2.167, -1.869, 20.0, 40.0), (3.016, -1.393, 7.294, 20.0, 40.0), (0.045, -1.928, 6.879, 20.0, 40.0), (-2.555, -0.984, 3.690, 20.0, 40.0), (-1.995, -0.054, 2.111, 20.0, 40.0), (4.600, -0.509, 6.318, 20.0, 40.0), (-1.942, 1.215, -0.402, 20.0, 40.0), (1.262, 2.765, -1.899, 20.0, 40.0), (2.617, -1.106, 6.521, 20.0, 40.0), (1.737, 0.554, 2.761, 20.0, 40.0), (-2.197, 0.632, 0.638, 20.0, 40.0), (4.768, 2.618, 0.147, 20.0, 40.0), (-3.737, -0.939, 3.010, 20.0, 40.0), (-2.623, 0.595, 0.499, 20.0, 40.0), (4.752, -0.340, 6.057, 20.0, 40.0), (2.333, -1.037, 6.240, 20.0, 40.0), (4.234, -1.882, 8.881, 20.0, 40.0), (-3.393, -0.812, 2.927, 20.0, 40.0), (0.885, 1.383, 0.678, 20.0, 40.0), (0.123, 2.937, -2.812, 20.0, 40.0), (2.969, 0.760, 2.964, 20.0, 40.0), (-4.929, 1.251, -1.967, 20.0, 40.0), (1.916, 2.223, -0.488, 20.0, 40.0), (-0.020, -1.740, 6.469, 20.0, 40.0), (0.702, -1.272, 5.895, 20.0, 40.0), (2.496, 2.648, -1.048, 20.0, 40.0), (4.067, -1.475, 7.984, 20.0, 40.0), (-3.717, 1.851, -2.561, 20.0, 40.0), (1.678, -0.624, 5.088, 20.0, 40.0), (1.073, 0.695, 2.146, 20.0, 40.0), (1.842, -0.749, 5.419, 20.0, 40.0), (-3.518, 1.909, -2.578, 20.0, 40.0), (2.229, 1.189, 1.737, 20.0, 40.0), (4.987, 2.893, -0.292, 20.0, 40.0), (-4.809, 1.043, -1.490, 20.0, 40.0), (-0.241, -0.728, 4.334, 20.0, 40.0), (-3.331, 0.590, 0.156, 20.0, 40.0), (-0.455, 2.621, -2.470, 20.0, 40.0), (1.492, 1.223, 1.301, 20.0, 40.0), (3.948, 2.841, -0.709, 20.0, 40.0), (0.732, 0.446, 2.475, 20.0, 40.0), (2.400, 2.390, -0.579, 20.0, 40.0), (-2.718, 1.427, -1.213, 20.0, 40.0), (-1.826, 1.451, -0.815, 20.0, 40.0), (1.125, 0.438, 2.686, 20.0, 40.0), (-4.918, 1.880, -3.219, 20.0, 40.0), (3.068, -0.442, 5.418, 20.0, 40.0), (1.982, 1.201, 1.589, 20.0, 40.0), (0.701, -1.709, 6.768, 20.0, 40.0), (-1.496, 2.564, -2.877, 20.0, 40.0), (-3.812, 0.974, -0.853, 20.0, 40.0), (-3.405, 2.018, -2.739, 20.0, 40.0), (2.211, 2.889, -1.674, 20.0, 40.0), (-2.481, 2.931, -4.103, 20.0, 40.0), (-3.721, 2.765, -4.391, 20.0, 40.0), (-1.768, -1.292, 4.699, 20.0, 40.0), (-4.462, 1.058, -1.347, 20.0, 40.0), (-3.516, -1.942, 5.126, 20.0, 40.0), (0.485, 2.420, -1.597, 20.0, 40.0), (-0.492, 0.242, 2.270, 20.0, 40.0), (4.245, 1.689, 1.744, 20.0, 40.0), (2.234, 0.364, 3.389, 20.0, 40.0), (2.629, 2.224, -0.134, 20.0, 40.0), (-4.375, 1.221, -1.630, 20.0, 40.0), (-0.618, 1.374, -0.057, 20.0, 40.0), (-2.580, -1.604, 4.918, 20.0, 40.0), (0.159, 1.104, 0.871, 20.0, 40.0), (-3.597, 0.975, -0.749, 20.0, 40.0);

DROP TABLE IF EXISTS test.model;
create table test.model engine = Memory as select stochasticLinearRegressionState(0.03, 0.00001, 2, 'Nesterov')(target, param1, param2) as state from test.defaults;

select ans > -67.0 and ans < -66.9 from
(with (select state from test.model) as model select evalMLMethod(model, predict1, predict2) as ans from test.defaults limit 1);

-- Check that returned weights are close to real
select ans > 0.49 and ans < 0.51 from
(select stochasticLinearRegression(0.03, 0.00001, 2, 'Nesterov')(target, param1, param2)[1] as ans from test.defaults);

select ans > -2.01 and ans < -1.99 from
(select stochasticLinearRegression(0.03, 0.00001, 2, 'Nesterov')(target, param1, param2)[2] as ans from test.defaults);

select ans > 2.99 and ans < 3.01 from
(select stochasticLinearRegression(0.03, 0.00001, 2, 'Nesterov')(target, param1, param2)[3] as ans from test.defaults);


-- Check GROUP BY

DROP TABLE IF EXISTS test.grouptest;
CREATE TABLE IF NOT EXISTS test.grouptest
(
    user_id UInt32,
    p1 Float64,
    p2 Float64,
    target Float64
) ENGINE = Memory;
INSERT INTO test.grouptest VALUES
(1, 1.732, 3.653, 11.422), (1, 2.150, 2.103, 7.609), (1, 0.061, 3.310, 7.052), (1, 1.030, 3.671, 10.075), (1, 1.879, 0.578, 2.492), (1, 0.922, 2.552, 6.499), (1, 1.145, -0.095, -0.993), (1, 1.920, 0.373, 1.959), (1, 0.458, 0.094, -1.801), (1, -0.118, 3.273, 6.582), (1, 2.667, 1.472, 6.752), (1, -0.387, -0.529, -5.360), (1, 2.219, 1.790, 6.810), (1, -0.754, 2.139, 1.908), (1, -0.446, -0.668, -5.896), (1, 1.729, 0.914, 3.199), (1, 2.908, -0.420, 1.556), (1, 1.645, 3.581, 11.034), (1, 0.358, -0.950, -5.136), (1, -0.467, 2.339, 3.084), (1, 3.629, 2.959, 13.135), (1, 2.393, 0.926, 4.563), (1, -0.945, 0.281, -4.047), (1, 3.688, -0.570, 2.667), (1, 3.016, 1.775, 8.356), (1, 2.571, 0.139, 2.559), (1, 2.999, 0.956, 5.866), (1, 1.754, -0.809, -1.920), (1, 3.943, 0.382, 6.030), (1, -0.970, 2.315, 2.004), (1, 1.503, 0.790, 2.376), (1, -0.775, 2.563, 3.139), (1, 1.211, 0.113, -0.240), (1, 3.058, 0.977, 6.048), (1, 2.729, 1.634, 7.360), (1, 0.307, 2.759, 5.893), (1, 3.272, 0.181, 4.089), (1, 1.192, 1.963, 5.273), (1, 0.931, 1.447, 3.203), (1, 3.835, 3.447, 15.011), (1, 0.709, 0.008, -1.559), (1, 3.155, -0.676, 1.283), (1, 2.342, 1.047, 4.824), (1, 2.059, 1.262, 4.903), (1, 2.797, 0.855, 5.159), (1, 0.387, 0.645, -0.292), (1, 1.418, 0.408, 1.060), (1, 2.719, -0.826, -0.039), (1, 2.735, 3.736, 13.678), (1, 0.205, 0.777, -0.260), (1, 3.117, 2.063, 9.424), (1, 0.601, 0.178, -1.263), (1, 0.064, 0.157, -2.401), (1, 3.104, -0.455, 1.842), (1, -0.253, 0.672, -1.490), (1, 2.592, -0.408, 0.961), (1, -0.909, 1.314, -0.878), (1, 0.625, 2.594, 6.031), (1, 2.749, -0.210, 1.869), (1, -0.469, 1.532, 0.657), (1, 1.954, 1.827, 6.388), (1, -0.528, 1.136, -0.647), (1, 0.802, -0.583, -3.146), (1, -0.176, 1.584, 1.400), (1, -0.705, -0.785, -6.766), (1, 1.660, 2.365, 7.416), (1, 2.278, 3.977, 13.485), (1, 2.846, 3.845, 14.229), (1, 3.588, -0.401, 2.974), (1, 3.525, 3.831, 15.542), (1, 0.191, 3.312, 7.318), (1, 2.615, -0.287, 1.370), (1, 2.701, -0.446, 1.064), (1, 2.065, -0.556, -0.538), (1, 2.572, 3.618, 12.997), (1, 3.743, -0.708, 2.362), (1, 3.734, 2.319, 11.425), (1, 3.768, 2.777, 12.866), (1, 3.203, 0.958, 6.280), (1, 1.512, 2.635, 7.927), (1, 2.194, 2.323, 8.356), (1, -0.726, 2.729, 3.735), (1, 0.020, 1.704, 2.152), (1, 2.173, 2.856, 9.912), (1, 3.124, 1.705, 8.364), (1, -0.834, 2.142, 1.759), (1, -0.702, 3.024, 4.666), (1, 1.393, 0.583, 1.535), (1, 2.136, 3.770, 12.581), (1, -0.445, 0.991, -0.917), (1, 0.244, -0.835, -5.016), (1, 2.789, 0.691, 4.652), (1, 0.246, 2.661, 5.475), (1, 3.793, 2.671, 12.601), (1, 1.645, -0.973, -2.627), (1, 2.405, 1.842, 7.336), (1, 3.221, 3.109, 12.769), (1, -0.638, 3.220, 5.385), (1, 1.836, 3.025, 9.748), (1, -0.660, 1.818, 1.133), (1, 0.901, 0.981, 1.744), (1, -0.236, 3.087, 5.789), (1, 1.744, 3.864, 12.078), (1, -0.166, 3.186, 6.226), (1, 3.536, -0.090, 3.803), (1, 3.284, 2.026, 9.648), (1, 1.327, 2.822, 8.119), (1, -0.709, 0.105, -4.104), (1, 0.509, -0.989, -4.949), (1, 0.180, -0.934, -5.440), (1, 3.522, 1.374, 8.168), (1, 1.497, -0.764, -2.297), (1, 1.696, 2.364, 7.482), (1, -0.202, -0.032, -3.500), (1, 3.109, -0.138, 2.804), (1, -0.238, 2.992, 5.501), (1, 1.639, 1.634, 5.181), (1, 1.919, 0.341, 1.859), (1, -0.563, 1.750, 1.124), (1, 0.886, 3.589, 9.539), (1, 3.619, 3.020, 13.299), (1, 1.703, -0.493, -1.073), (1, 2.364, 3.764, 13.022), (1, 1.820, 1.854, 6.201), (1, 1.437, -0.765, -2.421), (1, 1.396, 0.959, 2.668), (1, 2.608, 2.032, 8.312), (1, 0.333, -0.040, -2.455), (1, 3.441, 0.824, 6.355), (1, 1.303, 2.767, 7.908), (1, 1.359, 2.404, 6.932), (1, 0.674, 0.241, -0.930), (1, 2.708, -0.077, 2.183), (1, 3.821, 3.215, 14.287), (1, 3.316, 1.591, 8.404), (1, -0.848, 1.145, -1.259), (1, 3.455, 3.081, 13.153), (1, 2.568, 0.259, 2.914), (1, 2.866, 2.636, 10.642), (1, 2.776, -0.309, 1.626), (1, 2.087, 0.619, 3.031), (1, 1.682, 1.201, 3.967), (1, 3.800, 2.600, 12.399), (1, 3.344, -0.780, 1.347), (1, 1.053, -0.817, -3.346), (1, 0.805, 3.085, 7.865), (1, 0.173, 0.069, -2.449), (1, 2.018, 1.309, 4.964), (1, 3.713, 3.804, 15.838), (1, 3.805, -0.063, 4.421), (1, 3.587, 2.854, 12.738), (1, 2.426, -0.179, 1.315), (1, 0.535, 0.572, -0.213), (1, -0.558, 0.142, -3.690), (1, -0.875, 2.700, 3.349), (1, 2.405, 3.933, 13.610), (1, 1.633, 1.222, 3.934), (1, 0.049, 2.853, 5.657), (1, 1.146, 0.907, 2.015), (1, 0.300, 0.219, -1.744), (1, 2.226, 2.526, 9.029), (1, 2.545, -0.762, -0.198), (1, 2.553, 3.956, 13.974), (1, -0.898, 2.836, 3.713), (1, 3.796, -0.202, 3.985), (1, -0.810, 2.963, 4.268), (1, 0.511, 2.104, 4.334), (1, 3.527, 3.741, 15.275), (1, -0.921, 3.094, 4.440), (1, 0.856, 3.108, 8.036), (1, 0.815, 0.565, 0.323), (1, 3.717, 0.693, 6.512), (1, 3.052, 3.558, 13.778), (1, 2.942, 3.034, 11.986), (1, 0.765, 3.177, 8.061), (1, 3.175, -0.525, 1.776), (1, 0.309, 1.006, 0.638), (1, 1.922, 0.835, 3.349), (1, 3.678, 3.314, 14.297), (1, 2.840, -0.486, 1.221), (1, 1.195, 3.396, 9.578), (1, -0.157, 3.122, 6.053), (1, 2.404, 1.434, 6.110), (1, 3.108, 2.210, 9.845), (1, 2.289, 1.188, 5.142), (1, -0.319, -0.044, -3.769), (1, -0.625, 3.701, 6.854), (1, 2.269, -0.276, 0.710), (1, 0.777, 1.963, 4.442), (1, 0.411, 1.893, 3.501), (1, 1.173, 0.461, 0.728), (1, 1.767, 3.077, 9.765), (1, 0.853, 3.076, 7.933), (1, -0.013, 3.149, 6.421), (1, 3.841, 1.526, 9.260), (1, -0.950, 0.277, -4.070), (1, -0.644, -0.747, -6.527), (1, -0.923, 1.733, 0.353), (1, 0.044, 3.037, 6.201), (1, 2.074, 2.494, 8.631), (1, 0.016, 0.961, -0.085), (1, -0.780, -0.448, -5.904), (1, 0.170, 1.936, 3.148), (1, -0.420, 3.730, 7.349), (1, -0.630, 1.504, 0.254), (1, -0.006, 0.045, -2.879), (1, 1.101, -0.985, -3.753), (1, 1.618, 0.555, 1.900), (1, -0.336, 1.408, 0.552), (1, 1.086, 3.284, 9.024), (1, -0.815, 2.032, 1.466), (1, 3.144, -0.380, 2.148), (1, 2.326, 2.077, 7.883), (1, -0.571, 0.964, -1.251), (1, 2.416, 1.255, 5.595), (1, 3.964, 1.379, 9.065), (1, 3.897, 1.553, 9.455), (1, 1.806, 2.667, 8.611), (1, 0.323, 3.809, 9.073), (1, 0.501, 3.256, 7.769), (1, -0.679, 3.539, 6.259), (1, 2.825, 3.856, 14.219), (1, 0.288, -0.536, -4.032), (1, 3.009, 0.725, 5.193), (1, -0.763, 1.140, -1.105), (1, 1.124, 3.807, 10.670), (1, 2.478, 0.204, 2.570), (1, 2.825, 2.639, 10.566), (1, 1.878, -0.883, -1.892), (1, 3.380, 2.942, 12.587), (1, 2.202, 1.739, 6.621), (1, -0.711, -0.680, -6.463), (1, -0.266, 1.827, 1.951), (1, -0.846, 1.003, -1.683), (1, 3.201, 0.132, 3.798), (1, 2.797, 0.085, 2.849), (1, 1.632, 3.269, 10.072), (1, 2.410, 2.727, 10.003), (1, -0.624, 0.853, -1.690), (1, 1.314, 3.268, 9.433), (1, -0.395, 0.450, -2.440), (1, 0.992, 3.168, 8.489), (1, 3.355, 2.106, 10.028), (1, 0.509, -0.888, -4.647), (1, 1.007, 0.797, 1.405), (1, 0.045, 0.211, -2.278), (1, -0.911, 1.093, -1.544), (1, 2.409, 0.273, 2.637), (1, 2.640, 3.540, 12.899), (1, 2.668, -0.433, 1.038), (1, -0.014, 0.341, -2.005), (1, -0.525, -0.344, -5.083), (1, 2.278, 3.517, 12.105), (1, 3.712, 0.901, 7.128), (1, -0.689, 2.842, 4.149), (1, -0.467, 1.263, -0.147), (1, 0.963, -0.653, -3.034), (1, 2.559, 2.590, 9.889), (1, 1.566, 1.393, 4.312), (1, -1.000, 1.809, 0.429), (1, -0.297, 3.221, 6.070), (1, 2.199, 3.820, 12.856), (1, 3.096, 3.251, 12.944), (1, 1.479, 1.835, 5.461), (1, 0.276, 0.773, -0.130), (1, 0.607, 1.382, 2.360), (1, 1.169, -0.108, -0.985), (1, 3.429, 0.475, 5.282), (1, 2.626, 0.104, 2.563), (1, 1.156, 3.512, 9.850), (1, 3.947, 0.796, 7.282), (1, -0.462, 2.425, 3.351), (1, 3.957, 0.366, 6.014), (1, 3.763, -0.330, 3.536), (1, 0.667, 3.361, 8.417), (1, -0.583, 0.892, -1.492), (1, -0.505, 1.344, 0.021), (1, -0.474, 2.714, 4.195), (1, 3.455, 0.014, 3.950), (1, 1.016, 1.828, 4.516), (1, 1.845, 0.193, 1.269), (1, -0.529, 3.930, 7.731), (1, 2.636, 0.045, 2.408), (1, 3.757, -0.918, 1.760), (1, -0.808, 1.160, -1.137), (1, 0.744, 1.435, 2.793), (1, 3.457, 3.566, 14.613), (1, 1.061, 3.140, 8.544), (1, 3.733, 3.368, 14.570), (1, -0.969, 0.879, -2.301), (1, 3.940, 3.136, 14.287), (1, -0.730, 2.107, 1.860), (1, 3.699, 2.820, 12.858), (1, 2.197, -0.636, -0.514), (1, 0.775, -0.979, -4.387), (1, 2.019, 2.828, 9.521), (1, 1.415, 0.113, 0.170), (1, 1.567, 3.410, 10.363), (1, 0.984, -0.960, -3.913), (1, 1.809, 2.487, 8.079), (1, 1.550, 1.130, 3.489), (1, -0.770, 3.027, 4.542), (1, -0.358, 3.326, 6.262), (1, 3.140, 0.096, 3.567), (1, -0.685, 2.213, 2.270), (1, 0.916, 0.692, 0.907), (1, 1.526, 1.159, 3.527), (1, 2.675, -0.568, 0.645), (1, 1.740, 3.019, 9.538), (1, 1.223, 2.088, 5.709), (1, 1.572, -0.125, -0.230), (1, 3.641, 0.362, 5.369), (1, 2.944, 3.897, 14.578), (1, 2.775, 2.461, 9.932), (1, -0.200, 2.492, 4.076), (1, 0.065, 2.055, 3.296), (1, 2.375, -0.639, -0.167), (1, -0.133, 1.138, 0.149), (1, -0.385, 0.163, -3.281), (1, 2.200, 0.863, 3.989), (1, -0.470, 3.492, 6.536), (1, -0.916, -0.547, -6.472), (1, 0.634, 0.927, 1.049), (1, 2.930, 2.655, 10.825), (1, 3.094, 2.802, 11.596), (1, 0.457, 0.539, -0.470), (1, 1.277, 2.229, 6.240), (1, -0.157, 1.270, 0.496), (1, 3.320, 0.640, 5.559), (1, 2.836, 1.067, 5.872), (1, 0.921, -0.716, -3.307), (1, 3.886, 1.487, 9.233), (1, 0.306, -0.142, -2.815), (1, 3.727, -0.410, 3.225), (1, 1.268, -0.801, -2.866), (1, 2.302, 2.493, 9.084), (1, 0.331, 0.373, -1.220), (1, 3.224, -0.857, 0.879), (1, 1.328, 2.786, 8.014), (1, 3.639, 1.601, 9.081), (1, 3.201, -0.484, 1.949), (1, 3.447, -0.734, 1.692), (1, 2.773, -0.143, 2.117), (1, 1.517, -0.493, -1.445), (1, 1.778, -0.428, -0.728), (1, 3.989, 0.099, 5.274), (1, 1.126, 3.985, 11.206), (1, 0.348, 0.756, -0.035), (1, 2.399, 2.576, 9.525), (1, 0.866, 1.800, 4.132), (1, 3.612, 1.598, 9.017), (1, 0.495, 2.239, 4.707), (1, 2.442, 3.712, 13.019), (1, 0.238, -0.844, -5.057), (1, 1.404, 3.095, 9.093), (1, 2.842, 2.044, 8.816), (1, 0.622, 0.322, -0.791), (1, -0.561, 1.242, -0.395), (1, 0.679, 3.822, 9.823), (1, 1.875, 3.526, 11.327), (1, 3.587, 1.050, 7.324), (1, 1.467, 0.588, 1.699), (1, 3.180, 1.571, 8.074), (1, 1.402, 0.430, 1.093), (1, 1.834, 2.209, 7.294), (1, 3.542, -0.259, 3.306), (1, -0.517, 0.174, -3.513), (1, 3.549, 2.210, 10.729), (1, 2.260, 3.393, 11.699), (1, 0.036, 1.893, 2.751), (1, 0.680, 2.815, 6.804), (1, 0.219, 0.368, -1.459), (1, -0.519, 3.987, 7.924), (1, 0.974, 0.761, 1.231), (1, 0.107, 0.620, -0.927), (1, 1.513, 1.910, 5.755), (1, 3.114, 0.894, 5.910), (1, 3.061, 3.052, 12.276), (1, 2.556, 3.779, 13.448), (1, 1.964, 2.692, 9.002), (1, 3.894, -0.032, 4.690), (1, -0.693, 0.910, -1.655), (1, 2.692, 2.908, 11.108), (1, -0.824, 1.190, -1.078), (1, 3.621, 0.918, 6.997), (1, 3.190, 2.442, 10.707), (1, 1.424, -0.546, -1.791), (1, 2.061, -0.427, -0.158), (1, 1.532, 3.158, 9.540), (1, 0.648, 3.557, 8.967), (1, 2.511, 1.665, 7.017), (1, 1.903, -0.168, 0.302), (1, -0.186, -0.718, -5.528), (1, 2.421, 3.896, 13.531), (1, 3.063, 1.841, 8.650), (1, 0.636, 1.699, 3.367), (1, 1.555, 0.688, 2.174), (1, -0.412, 0.454, -2.462), (1, 1.645, 3.207, 9.911), (1, 3.396, 3.766, 15.090), (1, 0.375, -0.256, -3.017), (1, 3.636, 0.732, 6.469), (1, 2.503, 3.133, 11.405), (1, -0.253, 0.693, -1.429), (1, 3.178, 3.110, 12.686), (1, 3.282, -0.725, 1.388), (1, -0.297, 1.222, 0.070), (1, 1.872, 3.211, 10.377), (1, 3.471, 1.446, 8.278), (1, 2.891, 0.197, 3.374), (1, -0.896, 2.198, 1.802), (1, 1.178, -0.717, -2.796), (1, 0.650, 3.371, 8.412), (1, 0.447, 3.248, 7.637), (1, 1.616, -0.109, -0.097), (1, 1.837, 1.092, 3.951), (1, 0.767, 1.384, 2.684), (1, 3.466, -0.600, 2.133), (1, -0.800, -0.734, -6.802), (1, -0.534, 0.068, -3.865), (1, 3.416, -0.459, 2.455), (1, 0.800, -0.132, -1.795), (1, 2.150, 1.190, 4.869), (1, 0.830, 1.220, 2.319), (1, 2.656, 2.587, 10.072), (1, 0.375, -0.219, -2.906), (1, 0.582, -0.637, -3.749), (1, 0.588, -0.723, -3.992), (1, 3.875, 2.126, 11.127), (1, -0.476, 1.909, 1.775), (1, 0.963, 3.597, 9.716), (1, -0.888, 3.933, 7.021), (1, 1.711, -0.868, -2.184), (1, 3.244, 1.990, 9.460), (1, -0.057, 1.537, 1.497), (1, -0.015, 3.511, 7.504), (1, 0.280, 0.582, -0.695), (1, 2.402, 2.731, 9.998), (1, 2.053, 2.253, 7.865), (1, 1.955, 0.172, 1.424), (1, 3.746, 0.872, 7.107), (1, -0.157, 2.381, 3.829), (1, 3.548, -0.918, 1.340), (1, 2.449, 3.195, 11.482), (1, 1.582, 1.055, 3.329), (1, 1.908, -0.839, -1.700), (1, 2.341, 3.137, 11.091), (1, -0.043, 3.873, 8.532), (1, 0.528, -0.752, -4.198), (1, -0.940, 0.261, -4.098), (1, 2.609, 3.531, 12.812), (1, 2.439, 2.486, 9.336), (1, -0.659, -0.150, -4.768), (1, 2.131, 1.973, 7.181), (1, 0.253, 0.304, -1.583), (1, -0.169, 2.273, 3.480), (1, 1.855, 3.974, 12.631), (1, 0.092, 1.160, 0.666), (1, 3.990, 0.402, 6.187), (1, -0.455, 0.932, -1.113), (1, 2.365, 1.152, 5.185), (1, -0.058, 1.244, 0.618), (1, 0.674, 0.481, -0.209), (1, 3.002, 0.246, 3.743), (1, 1.804, 3.765, 11.902), (1, 3.567, -0.752, 1.876), (1, 0.098, 2.257, 3.968), (1, 0.130, -0.889, -5.409), (1, 0.633, 1.891, 3.940), (1, 0.421, 2.533, 5.440), (1, 2.252, 1.853, 7.063), (1, 3.191, -0.980, 0.443), (1, -0.776, 3.241, 5.171), (1, 0.509, 1.737, 3.229), (1, 3.583, 1.274, 7.986), (1, 1.101, 2.896, 7.891), (1, 3.072, -0.008, 3.120), (1, 2.945, -0.295, 2.006), (1, 3.621, -0.161, 3.760), (1, 1.399, 3.759, 11.075), (1, 3.783, -0.866, 1.968), (1, -0.241, 2.902, 5.225), (1, 1.323, 1.934, 5.449), (1, 1.449, 2.855, 8.464), (1, 0.088, 1.526, 1.753), (1, -1.000, 2.161, 1.485), (1, -0.214, 3.358, 6.647), (1, -0.384, 3.230, 5.921), (1, 3.146, 1.228, 6.975), (1, 1.917, 0.860, 3.415), (1, 1.982, 1.735, 6.167), (1, 1.404, 1.851, 5.360), (1, 2.428, -0.674, -0.166), (1, 2.081, -0.505, -0.352), (1, 0.914, -0.543, -2.802), (1, -0.029, -0.482, -4.506), (1, 0.671, 0.184, -1.105), (1, 1.641, -0.524, -1.292), (1, 1.005, 0.361, 0.094), (1, -0.493, 3.582, 6.760), (2, 3.876, 2.563, 21.500), (2, 0.159, -0.309, 7.986), (2, -0.496, 0.417, 12.998), (2, -0.164, -0.512, 7.092), (2, 0.632, 3.200, 28.571), (2, 3.772, 0.493, 9.188), (2, 2.430, -0.797, 2.789), (2, 3.872, -0.775, 1.475), (2, -0.031, -0.256, 8.495), (2, 2.726, 3.000, 25.271), (2, 1.116, -0.269, 7.269), (2, 0.551, 3.402, 29.860), (2, 0.820, 2.500, 24.179), (2, 1.153, -0.453, 6.131), (2, -0.717, -0.360, 8.556), (2, 0.532, 0.531, 12.654), (2, 2.096, 0.981, 13.791), (2, 0.146, -0.433, 7.259), (2, 1.000, 1.075, 15.452), (2, 2.963, -0.090, 6.495), (2, 1.047, 2.052, 21.267), (2, 0.882, 1.778, 19.785), (2, 1.380, 2.702, 24.832), (2, 1.853, 0.401, 10.554), (2, 2.004, 1.770, 18.618), (2, 3.377, 0.772, 11.253), (2, 1.227, -0.169, 7.759), (2, 0.428, 2.052, 21.885), (2, 0.070, 3.648, 31.816), (2, 0.128, -0.938, 4.244), (2, 2.061, 0.753, 12.454), (2, 1.207, -0.301, 6.989), (2, -0.168, 3.765, 32.757), (2, 3.450, 1.801, 17.353), (2, -0.483, 3.344, 30.547), (2, 1.847, 1.884, 19.455), (2, 3.241, 2.369, 20.975), (2, 0.628, 3.590, 30.912), (2, 2.183, 1.741, 18.263), (2, 0.774, 2.638, 25.057), (2, 3.292, 2.867, 23.912), (2, 0.056, 2.651, 25.850), (2, -0.506, 0.300, 12.308), (2, 0.524, 1.182, 16.570), (2, -0.267, 2.563, 25.647), (2, 3.953, -0.334, 4.040), (2, 2.507, 2.319, 21.408), (2, -0.770, 1.017, 16.875), (2, 0.481, 1.591, 19.062), (2, 3.243, 1.060, 13.114), (2, 2.178, -0.325, 5.873), (2, 2.510, 1.235, 14.900), (2, 2.684, 2.370, 21.535), (2, 3.466, 3.656, 28.469), (2, 2.994, 3.960, 30.764), (2, -0.363, 3.592, 31.917), (2, 1.738, 0.074, 8.708), (2, 1.462, 3.727, 30.902), (2, 0.059, 0.180, 11.021), (2, 2.980, 2.317, 20.925), (2, 1.248, 0.965, 14.545), (2, 0.776, -0.229, 7.850), (2, -0.562, 2.839, 27.598), (2, 3.581, 0.244, 7.883), (2, -0.958, 0.901, 16.362), (2, 3.257, 0.364, 8.925), (2, 1.478, 1.718, 18.827), (2, -0.121, -0.436, 7.507), (2, 0.966, 1.444, 17.697), (2, 3.631, 3.463, 27.144), (2, 0.174, -0.663, 5.848), (2, 2.783, 0.124, 7.959), (2, 1.106, -0.936, 3.276), (2, 0.186, -0.942, 4.162), (2, 3.513, 2.456, 21.222), (2, 0.339, 2.316, 23.558), (2, 0.566, 2.515, 24.523), (2, -0.134, 0.746, 14.607), (2, 1.554, 0.106, 9.084), (2, -0.846, 2.748, 27.337), (2, 3.934, 0.564, 9.451), (2, 2.840, -0.966, 1.366), (2, 1.379, 0.307, 10.463), (2, 1.065, -0.780, 4.253), (2, 3.324, 2.145, 19.546), (2, 0.974, -0.543, 5.767), (2, 2.469, 3.976, 31.385), (2, -0.434, 3.689, 32.570), (2, 0.261, 0.481, 12.624), (2, 3.786, 2.605, 21.843), (2, -0.460, -0.536, 7.243), (2, 2.576, 2.880, 24.702), (2, -0.501, 3.551, 31.810), (2, 2.946, 3.263, 26.633), (2, 2.959, -0.813, 2.162), (2, -0.749, 0.490, 13.686), (2, 2.821, 0.335, 9.187), (2, 3.964, 0.272, 7.667), (2, 0.808, -0.700, 4.994), (2, 0.415, 2.183, 22.682), (2, 2.551, 3.785, 30.156), (2, 0.821, 1.120, 15.897), (2, 1.714, 3.019, 26.400), (2, 2.265, 1.950, 19.438), (2, 1.493, 3.317, 28.409), (2, -0.445, 2.282, 24.134), (2, -0.508, 2.508, 25.553), (2, 1.017, -0.621, 5.255), (2, 1.053, 2.246, 22.422), (2, 0.441, 1.637, 19.382), (2, 3.657, 1.246, 13.816), (2, 0.756, 0.808, 14.095), (2, 1.849, 1.599, 17.742), (2, 1.782, -0.000, 8.215), (2, 1.136, 3.940, 32.506), (2, 2.814, 3.288, 26.916), (2, 3.180, 3.198, 26.008), (2, 0.728, -0.054, 8.946), (2, 0.801, 0.775, 13.852), (2, 1.399, -0.546, 5.322), (2, 1.415, 1.753, 19.103), (2, 2.860, 1.796, 17.913), (2, 0.712, 2.902, 26.699), (2, -0.389, 3.093, 28.945), (2, 3.661, 3.666, 28.333), (2, 3.944, 0.996, 12.030), (2, 1.655, 1.385, 16.657), (2, 0.122, -0.662, 5.906), (2, 3.667, 2.763, 22.912), (2, 2.606, 0.630, 11.172), (2, -0.291, 1.492, 19.242), (2, -0.787, 1.223, 18.125), (2, 2.405, 0.325, 9.545), (2, 3.129, -0.412, 4.398), (2, 0.588, 3.964, 33.194), (2, -0.177, 3.636, 31.993), (2, 2.079, 3.280, 27.603), (2, 3.055, 3.958, 30.692), (2, -0.164, 3.188, 29.292), (2, 3.803, 3.151, 25.105), (2, 3.123, -0.891, 1.531), (2, 3.070, -0.824, 1.988), (2, 3.103, -0.931, 1.309), (2, 0.589, 3.353, 29.529), (2, 1.095, 1.973, 20.744), (2, -0.557, 0.370, 12.775), (2, 1.223, 0.307, 10.620), (2, 3.255, -0.768, 2.136), (2, 0.508, 2.157, 22.435), (2, 0.373, 0.319, 11.544), (2, 1.240, 1.736, 19.177), (2, 1.846, 0.970, 13.972), (2, 3.352, -0.534, 3.445), (2, -0.352, -0.290, 8.610), (2, 0.281, 0.193, 10.880), (2, 3.450, -0.059, 6.193), (2, 0.310, 2.575, 25.140), (2, 1.791, 1.127, 14.970), (2, 1.992, 2.347, 22.087), (2, -0.288, 2.881, 27.576), (2, 3.464, 3.664, 28.518), (2, 0.573, 2.789, 26.159), (2, 2.265, 1.583, 17.233), (2, 3.203, 0.730, 11.177), (2, 3.345, 1.368, 14.862), (2, 0.891, 3.690, 31.248), (2, 2.252, -0.311, 5.884), (2, -0.087, 0.804, 14.912), (2, 0.153, 2.510, 24.905), (2, 3.533, -0.965, 0.675), (2, 2.035, 1.953, 19.683), (2, 0.316, 2.448, 24.373), (2, 2.199, 3.858, 30.946), (2, -0.519, 3.647, 32.399), (2, 0.867, 1.961, 20.901), (2, 2.739, 2.268, 20.866), (2, 2.462, -0.664, 3.551), (2, 1.372, 3.419, 29.144), (2, -0.628, 2.723, 26.968), (2, 3.989, -0.225, 4.659), (2, 0.166, 3.190, 28.976), (2, 1.681, 2.937, 25.943), (2, 2.979, 2.263, 20.600), (2, 3.896, -0.419, 3.590), (2, 3.861, 2.224, 19.485), (2, -0.087, -0.861, 4.918), (2, 1.182, 1.886, 20.133), (2, 3.622, 2.320, 20.301), (2, 3.560, 0.008, 6.491), (2, 3.082, -0.605, 3.285), (2, 1.777, 1.324, 16.169), (2, 2.269, 2.436, 22.348), (2, 0.019, 3.074, 28.423), (2, -0.560, 3.868, 33.765), (2, 1.568, 2.886, 25.749), (2, 2.045, 0.222, 9.286), (2, 1.391, 0.352, 10.723), (2, 0.172, 1.908, 21.276), (2, 1.173, -0.726, 4.474), (2, 1.642, 2.576, 23.814), (2, 3.346, 1.377, 14.918), (2, 0.120, 0.411, 12.344), (2, 3.913, 0.820, 11.008), (2, 1.054, 3.732, 31.340), (2, 2.284, 0.108, 8.362), (2, 2.266, 0.066, 8.131), (2, 3.204, 1.156, 13.735), (2, 3.243, 2.032, 18.947), (2, 3.052, -0.121, 6.221), (2, 1.131, 2.189, 22.000), (2, 2.958, 0.658, 10.990), (2, 1.717, 3.708, 30.530), (2, 2.417, 2.070, 20.004), (2, 2.175, 0.881, 13.110), (2, 0.333, 3.494, 30.629), (2, 3.598, 3.940, 30.044), (2, 3.683, -0.110, 5.660), (2, 2.555, 1.196, 14.620), (2, 1.511, 0.453, 11.206), (2, 0.903, 1.390, 17.439), (2, -0.897, 3.303, 30.716), (2, 0.245, 2.129, 22.527), (2, 1.370, 2.715, 24.923), (2, 1.822, -0.917, 2.676), (2, 2.690, -0.109, 6.657), (2, 0.206, 1.561, 19.162), (2, 3.905, 2.710, 22.357), (2, -0.438, 3.207, 29.678), (2, 0.898, 3.445, 29.772), (2, 1.838, 2.871, 25.385), (2, 0.116, 1.401, 18.292), (2, -0.408, 2.375, 24.656), (2, 1.681, 3.338, 28.349), (2, 1.177, -0.318, 6.914), (2, 1.004, 0.626, 12.753), (2, 2.840, 2.589, 22.691), (2, 1.258, 3.993, 32.700), (2, 2.016, 3.489, 28.920), (2, -0.728, 0.164, 11.713), (2, 0.193, 1.479, 18.682), (2, 2.647, -0.969, 1.541), (2, 3.837, 2.602, 21.773), (2, 0.541, 0.205, 10.690), (2, 0.026, 2.756, 26.511), (2, 0.924, 0.909, 14.530), (2, 0.974, -0.074, 8.581), (2, 0.081, 0.005, 9.948), (2, 1.331, 2.942, 26.320), (2, 2.498, 3.405, 27.934), (2, 3.741, 1.554, 15.581), (2, 3.502, -0.089, 5.964), (2, 3.069, 1.768, 17.539), (2, 3.115, -0.008, 6.839), (2, 3.237, -0.503, 3.745), (2, 0.768, -0.135, 8.420), (2, 0.410, 3.974, 33.437), (2, 0.238, -0.700, 5.564), (2, 3.619, 0.350, 8.482), (2, 3.563, 3.059, 24.788), (2, 2.916, 3.101, 25.691), (2, 0.144, 3.282, 29.549), (2, 1.288, 2.642, 24.565), (2, -0.859, 0.229, 12.234), (2, 1.507, -0.711, 4.229), (2, -0.634, 2.608, 26.281), (2, 2.054, -0.834, 2.942), (2, 0.453, 1.072, 15.980), (2, 3.914, 1.159, 13.039), (2, 0.254, 1.835, 20.758), (2, 1.577, 0.428, 10.991), (2, 1.990, 3.569, 29.421), (2, 1.584, 1.803, 19.234), (2, 0.835, 3.603, 30.785), (2, 0.900, 3.033, 27.296), (2, 1.180, 0.280, 10.499), (2, 2.400, 2.802, 24.409), (2, 0.924, 2.462, 23.851), (2, 2.138, 0.722, 12.192), (2, -0.253, -0.809, 5.401), (2, 3.570, -0.116, 5.733), (2, 0.201, -0.182, 8.708), (2, 2.457, 0.454, 10.267), (2, -0.053, 0.443, 12.709), (2, 2.108, 2.069, 20.309), (2, -0.964, -0.441, 8.318), (2, 1.802, 0.403, 10.614), (2, 3.704, 3.902, 29.711), (2, 1.904, 2.418, 22.603), (2, 2.965, 3.429, 27.606), (2, -0.801, -0.072, 10.370), (2, 3.009, 0.491, 9.937), (2, 2.781, 1.026, 13.376), (2, -0.421, 0.744, 14.883), (2, 3.639, -0.148, 5.476), (2, 0.584, 2.041, 21.663), (2, 1.547, -0.391, 6.107), (2, -0.204, 0.727, 14.564), (2, 0.372, 0.464, 12.410), (2, 1.185, 1.732, 19.207), (2, 3.574, 0.755, 10.954), (2, 2.164, 1.425, 16.385), (2, 1.895, 1.374, 16.351), (2, 2.352, 2.188, 20.779), (2, 0.187, 0.677, 13.874), (2, -0.589, 3.686, 32.703), (2, 3.081, 0.414, 9.403), (2, 3.341, 3.246, 26.137), (2, 0.617, -0.201, 8.174), (2, 1.518, 3.833, 31.481), (2, 2.613, -0.350, 5.286), (2, 3.426, 0.751, 11.082), (2, 2.726, 3.586, 28.787), (2, 2.834, -0.219, 5.855), (2, 1.038, 3.607, 30.605), (2, 0.479, 1.226, 16.874), (2, 1.729, 0.297, 10.053), (2, 0.050, 1.815, 20.841), (2, -0.554, 3.538, 31.782), (2, 2.773, 0.973, 13.064), (2, -0.239, 3.425, 30.786), (2, 3.611, 3.700, 28.590), (2, 1.418, 3.625, 30.332), (2, 1.599, 1.626, 18.156), (2, 1.841, 1.518, 17.269), (2, 1.119, 1.996, 20.856), (2, 2.810, 2.293, 20.947), (2, 1.174, 2.062, 21.198), (2, -0.326, -0.279, 8.655), (2, -0.365, 0.816, 15.259), (2, 1.296, -0.095, 8.132), (2, -0.263, 0.511, 13.327), (2, 1.757, 3.012, 26.314), (2, 1.849, 1.065, 14.539), (2, 1.651, 2.244, 21.814), (2, 3.942, 1.026, 12.214), (2, 2.314, 1.944, 19.353), (2, 3.055, -0.002, 6.930), (2, 0.402, 1.350, 17.698), (2, 0.004, 2.288, 23.724), (2, 3.265, 2.962, 24.509), (2, 1.044, -0.684, 4.850), (2, -0.280, 2.278, 23.948), (2, 1.216, 0.726, 13.142), (2, 3.181, 3.518, 27.925), (2, 3.199, -0.124, 6.055), (2, 0.510, -0.622, 5.755), (2, 2.920, 1.067, 13.484), (2, 2.573, 1.844, 18.492), (2, 1.155, 3.505, 29.878), (2, 2.033, 1.756, 18.502), (2, 1.312, 0.114, 9.373), (2, -0.823, 3.339, 30.854), (2, 0.287, 3.891, 33.060), (2, -0.621, -0.210, 9.363), (2, 3.734, 1.574, 15.712), (2, -0.932, 0.772, 15.561), (2, -0.719, 1.604, 20.345), (2, -0.555, 0.773, 15.190), (2, -0.744, 3.934, 34.348), (2, 1.671, -0.425, 5.778), (2, 2.754, 2.690, 23.385), (2, 1.826, 2.185, 21.283), (2, 1.970, 0.021, 8.159), (2, 2.882, 3.494, 28.081), (2, 1.668, -0.030, 8.150), (2, 0.472, 2.184, 22.633), (2, 1.656, 3.393, 28.701), (2, -0.069, 2.331, 24.057), (2, 0.075, 1.341, 17.973), (2, 1.836, 0.565, 11.554), (2, -0.235, 0.520, 13.357), (2, 3.620, 3.169, 25.393), (2, 0.401, -0.062, 9.224), (2, 1.503, 1.667, 18.501), (2, 3.727, 1.149, 13.166), (2, 2.777, -0.081, 6.737), (2, 3.914, -0.234, 4.680), (2, 1.765, 0.750, 12.737), (2, 1.746, 1.818, 19.161), (2, 0.019, 2.819, 26.893), (2, 1.068, 1.917, 20.434), (2, 3.035, 3.158, 25.915), (2, 2.012, 0.724, 12.330), (2, 2.597, 2.264, 20.986), (2, 3.428, 3.239, 26.005), (2, -0.016, -0.529, 6.842), (2, 1.314, 0.735, 13.095), (2, 2.832, -0.567, 3.768), (2, -0.296, 2.641, 26.141), (2, 2.863, 3.889, 30.470), (2, 2.849, 3.997, 31.130), (2, 1.660, 1.813, 19.216), (2, 2.798, 0.977, 13.062), (2, 3.935, 0.549, 9.359), (2, 1.002, 3.557, 30.342), (2, 3.052, 2.207, 20.193), (2, 3.455, 0.458, 9.294), (2, 3.312, 2.138, 19.515), (2, 0.292, 0.058, 10.056), (2, 0.050, -0.211, 8.682), (2, -0.215, 1.108, 16.866), (2, -0.169, 0.647, 14.048), (2, 2.546, 0.876, 12.709), (2, -0.911, -0.209, 9.659), (2, 0.950, 2.894, 26.413), (2, -0.512, -0.167, 9.508), (2, 1.821, -0.747, 3.696), (2, 2.257, 3.945, 31.415), (2, 2.398, -0.586, 4.087), (2, 3.051, 0.815, 11.836), (2, 3.399, 2.131, 19.389), (2, 2.982, 1.549, 16.314), (2, -0.790, -0.329, 8.819), (2, 3.797, 0.327, 8.167), (2, 1.838, 0.290, 9.902), (2, 1.906, 1.782, 18.785), (2, 1.330, -0.208, 7.422), (2, -0.217, 0.854, 15.344), (2, 3.310, 1.582, 16.180), (2, 2.965, 0.917, 12.537), (2, 3.558, -0.164, 5.460), (2, -0.841, 2.060, 23.203), (2, 2.892, 2.621, 22.834), (2, -0.011, -0.198, 8.821), (2, -0.430, 2.999, 28.424), (2, -0.584, 0.894, 15.946), (2, 0.033, 1.310, 17.829), (2, 3.044, 0.410, 9.418), (2, 3.932, 0.295, 7.836), (2, 0.394, 1.315, 17.494), (2, 1.424, -0.167, 7.573), (2, 1.676, 1.118, 15.031), (2, 1.821, 0.714, 12.462), (2, 2.688, 1.497, 16.292), (2, 3.960, 2.344, 20.103), (2, -0.787, -0.161, 9.819), (2, 3.538, 3.651, 28.366), (2, -0.338, 0.458, 13.088), (2, -0.146, 3.162, 29.120), (2, 3.124, 3.352, 26.989), (2, -0.189, 3.685, 32.301), (2, 0.396, 1.004, 15.626), (2, -0.171, 2.114, 22.858), (2, 3.736, 0.732, 10.659), (2, 1.259, 2.564, 24.127), (2, -0.263, 2.426, 24.820), (2, 1.558, -0.858, 3.292), (2, 2.882, 1.110, 13.776), (2, 0.039, 1.284, 17.666), (2, 3.074, 2.379, 21.201), (2, -0.523, 0.303, 12.344), (2, 0.363, 1.082, 16.132), (2, 2.925, 2.187, 20.195), (2, 0.595, -0.335, 7.397), (2, 0.062, -0.232, 8.544), (2, 0.877, 2.155, 22.050), (2, -0.256, 2.922, 27.788), (2, 1.813, 3.161, 27.152), (2, 2.177, 2.532, 23.016), (2, -0.051, 0.035, 10.263), (2, 2.688, 3.599, 28.906), (2, 2.539, -0.076, 7.008), (2, 2.563, 1.467, 16.240), (2, -0.755, 2.276, 24.410), (2, 3.092, 0.660, 10.868), (2, 2.403, 2.693, 23.756), (2, -0.170, 2.178, 23.239), (2, 2.672, -0.603, 3.712), (2, -0.077, -0.493, 7.116), (2, 1.997, 1.934, 19.608), (2, 1.913, -0.792, 3.335), (2, 0.171, -0.329, 7.857), (2, 2.488, 0.171, 8.540), (2, -0.514, 0.331, 12.500), (2, -0.201, 2.484, 25.103), (2, 2.436, 0.032, 7.759), (2, -0.094, 2.530, 25.275), (2, 2.186, 2.591, 23.358), (2, 3.171, -0.766, 2.231), (2, 2.410, 0.183, 8.687), (2, -0.699, -0.329, 8.728), (2, 3.285, 2.252, 20.228), (2, 1.928, -0.059, 7.720), (2, 3.460, 0.399, 8.931), (2, 2.542, 0.224, 8.801), (2, 2.902, 2.101, 19.702), (2, 3.808, 2.528, 21.358), (2, 0.330, 0.642, 13.522), (2, -0.088, 1.286, 17.804), (2, 3.025, 2.354, 21.100), (2, 3.306, 2.049, 18.986), (2, 1.477, 1.720, 18.845), (2, 2.676, 3.601, 28.931), (2, 1.577, 0.170, 9.443), (2, 1.362, 3.534, 29.843), (2, 2.616, 3.106, 26.018), (2, 3.773, 0.378, 8.496), (2, -0.125, 2.057, 22.465), (2, 3.174, 1.382, 15.120), (2, 0.844, 2.058, 21.503);

SELECT ANS[1] > -1.1 AND ANS[1] < -0.9 AND ANS[2] > 5.9 AND ANS[2] < 6.1 AND ANS[3] > 9.9 AND ANS[3] < 10.1 FROM
(SELECT stochasticLinearRegression(0.05, 0, 1)(target, p1, p2) AS ANS FROM test.grouptest GROUP BY user_id limit 0,1);

SELECT ANS[1] > 1.9 AND ANS[1] < 2.1 AND ANS[2] > 2.9 AND ANS[2] < 3.1 AND ANS[3] > -3.1 AND ANS[3] < -2.9 FROM
(SELECT stochasticLinearRegression(0.05, 0, 1)(target, p1, p2) AS ANS FROM test.grouptest GROUP BY user_id limit 1, 1);