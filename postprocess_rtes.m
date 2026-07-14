%% RTES 24-hour post-processing
% Place this script beside metrics_log.txt and diagnostics_log.csv.
% It validates the data, computes statistics, and exports the three
% figures required by the assignment.

clear; clc; close all;

M = readtable("metrics_log.txt", "Delimiter", ",");
D = readtable("diagnostics_log.csv", "Delimiter", ",");

%% Validation
requiredMetrics = ["Seconds","Nanoseconds","Commit_Count","Identity_Count", ...
    "Account_Count","Info_Count","Buffer_Occupancy_Pct","CPU_Pct"];
requiredDiagnostics = ["Realtime_Seconds","Realtime_Nanoseconds", ...
    "Interval_Unknown","Total_Unknown","Interval_Dropped","Total_Dropped", ...
    "Interval_Truncated","Total_Truncated","Current_Queue","Max_Queue"];

assert(all(ismember(requiredMetrics, string(M.Properties.VariableNames))), ...
    "The metrics file does not contain all required columns.");
assert(all(ismember(requiredDiagnostics, string(D.Properties.VariableNames))), ...
    "The diagnostics file does not contain all expected columns.");
assert(height(M) == 86400, "Expected exactly 86400 metrics rows.");
assert(height(D) == height(M), "Metrics and diagnostics row counts differ.");
assert(all(M.Seconds == D.Realtime_Seconds) && ...
       all(M.Nanoseconds == D.Realtime_Nanoseconds), ...
       "Metrics and diagnostics timestamps are not aligned.");

%% Time and derived quantities
% Work with separate second/nanosecond deltas to preserve precision.
deltaNs = diff(double(M.Seconds)) * 1e9 + diff(double(M.Nanoseconds));
dt = deltaNs / 1e9;
jitterMs = (deltaNs - 1e9) / 1e6;

elapsedNs = (double(M.Seconds) - double(M.Seconds(1))) * 1e9 + ...
            (double(M.Nanoseconds) - double(M.Nanoseconds(1)));
elapsedHours = elapsedNs / 3.6e12;
jitterHours = elapsedHours(2:end);

messageRate = M.Commit_Count + M.Identity_Count + ...
              M.Account_Count + M.Info_Count;
cpuIdle = 100 - M.CPU_Pct;

%% Numerical summary
fprintf("\n===== 24-hour summary =====\n");
fprintf("Samples: %d\n", height(M));
fprintf("Total classified messages: %d\n", sum(messageRate));
fprintf("Mean message rate: %.3f Hz\n", mean(messageRate));
fprintf("Median message rate: %.3f Hz\n", median(messageRate));
fprintf("Maximum message rate: %d Hz\n", max(messageRate));
fprintf("Mean CPU usage: %.3f %%\n", mean(M.CPU_Pct));
fprintf("Maximum CPU usage: %.3f %%\n", max(M.CPU_Pct));
fprintf("Mean CPU idle: %.3f %%\n", mean(cpuIdle));
fprintf("Maximum logged buffer occupancy: %.3f %%\n", ...
    max(M.Buffer_Occupancy_Pct));
fprintf("Mean interval: %.12f s\n", mean(dt));
fprintf("Minimum interval: %.12f s\n", min(dt));
fprintf("Maximum interval: %.12f s\n", max(dt));
fprintf("Maximum absolute jitter: %.6f ms\n", max(abs(jitterMs)));
fprintf("99th percentile absolute jitter: %.6f ms\n", ...
    prctile(abs(jitterMs), 99));
fprintf("Correlation(rate, CPU): %.4f\n", corr(messageRate, M.CPU_Pct));
fprintf("Correlation(rate, buffer): %.4f\n", ...
    corr(messageRate, M.Buffer_Occupancy_Pct));
fprintf("Unknown messages: %d\n", sum(D.Interval_Unknown));
fprintf("Dropped messages: %d\n", sum(D.Interval_Dropped));
fprintf("Truncated messages: %d\n", sum(D.Interval_Truncated));
fprintf("Maximum current queue depth: %d\n", max(D.Current_Queue));
fprintf("Maximum interval queue depth: %d\n", max(D.Max_Queue));

%% Figure 1: Jitter
figure("Color","w");
plot(jitterHours, jitterMs, "LineWidth", 0.5);
yline(0, "--");
grid on;
xlabel("Elapsed time (hours)");
ylabel("Jitter (ms)");
title("Periodic Monitor Jitter Over 24 Hours");
exportgraphics(gcf, "figure1_jitter.png", "Resolution", 300);

%% Figure 2: Network load and buffer occupancy
figure("Color","w");
yyaxis left
plot(elapsedHours, messageRate, "LineWidth", 0.5);
ylabel("Message rate (Hz)");
yyaxis right
plot(elapsedHours, M.Buffer_Occupancy_Pct, "LineWidth", 0.5);
ylabel("Buffer occupancy (%)");
grid on;
xlabel("Elapsed time (hours)");
title("Incoming Message Rate and Buffer Occupancy");
exportgraphics(gcf, "figure2_load_buffer.png", "Resolution", 300);

%% Figure 3: CPU usage and incoming rate
figure("Color","w");
scatter(messageRate, M.CPU_Pct, 6, "filled", ...
    "MarkerFaceAlpha", 0.20, "MarkerEdgeAlpha", 0.20);
hold on;
p = polyfit(messageRate, M.CPU_Pct, 1);
xFit = linspace(min(messageRate), max(messageRate), 200);
plot(xFit, polyval(p, xFit), "LineWidth", 1.5);
grid on;
xlabel("Incoming message rate (Hz)");
ylabel("CPU usage (%)");
title("CPU Usage Versus Incoming Message Rate");
exportgraphics(gcf, "figure3_cpu_vs_rate.png", "Resolution", 300);

%% Save summary table
Summary = table( ...
    height(M), sum(messageRate), mean(messageRate), median(messageRate), ...
    max(messageRate), mean(M.CPU_Pct), max(M.CPU_Pct), ...
    max(M.Buffer_Occupancy_Pct), mean(dt), min(dt), max(dt), ...
    max(abs(jitterMs)), prctile(abs(jitterMs),99), ...
    corr(messageRate,M.CPU_Pct), ...
    corr(messageRate,M.Buffer_Occupancy_Pct), ...
    sum(D.Interval_Unknown), sum(D.Interval_Dropped), ...
    sum(D.Interval_Truncated), max(D.Current_Queue), max(D.Max_Queue), ...
    'VariableNames', { ...
    'Samples','TotalMessages','MeanRateHz','MedianRateHz','MaxRateHz', ...
    'MeanCpuPct','MaxCpuPct','MaxBufferPct','MeanIntervalS', ...
    'MinIntervalS','MaxIntervalS','MaxAbsJitterMs','P99AbsJitterMs', ...
    'RateCpuCorrelation','RateBufferCorrelation','UnknownMessages', ...
    'DroppedMessages','TruncatedMessages','MaxCurrentQueue', ...
    'MaxIntervalQueue'});
writetable(Summary, "summary_statistics.csv");
disp(Summary);
