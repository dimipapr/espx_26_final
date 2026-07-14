%% Create exact 24-hour logs from the continuous July 12 run
clear; clc;

sourceFolder = fullfile(pwd, 'collected_data');
metricsSource = fullfile(sourceFolder, 'metrics_20260712_000515.csv');
diagnosticsSource = fullfile(sourceFolder, 'diagnostics_20260712_000515.csv');

samplesPerDay = 24 * 60 * 60;  % 86,400 one-second samples

metrics = readtable(metricsSource, 'VariableNamingRule', 'preserve');
diagnostics = readtable(diagnosticsSource, 'VariableNamingRule', 'preserve');

assert(height(metrics) >= samplesPerDay, ...
    'The metrics source does not contain 24 hours of samples.');
assert(height(diagnostics) >= samplesPerDay, ...
    'The diagnostics source does not contain 24 hours of samples.');

% Keep matching rows from the same continuous execution.
metrics24h = metrics(1:samplesPerDay, :);
diagnostics24h = diagnostics(1:samplesPerDay, :);

% Confirm timestamps match row by row.
assert(all(metrics24h.Seconds == diagnostics24h.Realtime_Seconds));
assert(all(metrics24h.Nanoseconds == diagnostics24h.Realtime_Nanoseconds));

writetable(metrics24h, 'metrics_log.txt', 'Delimiter', ',');
writetable(diagnostics24h, 'diagnostics_log.csv');

startTime = datetime(metrics24h.Seconds(1), 'ConvertFrom', 'posixtime', ...
    'TimeZone', 'Europe/Athens') + seconds(metrics24h.Nanoseconds(1) / 1e9);
endTime = datetime(metrics24h.Seconds(end), 'ConvertFrom', 'posixtime', ...
    'TimeZone', 'Europe/Athens') + seconds(metrics24h.Nanoseconds(end) / 1e9);

fprintf('Created metrics_log.txt with %d samples.\n', height(metrics24h));
fprintf('Created diagnostics_log.csv with %d samples.\n', height(diagnostics24h));
fprintf('First sample: %s\n', string(startTime, 'yyyy-MM-dd HH:mm:ss.SSSSSSSSS Z'));
fprintf('Last sample:  %s\n', string(endTime, 'yyyy-MM-dd HH:mm:ss.SSSSSSSSS Z'));
