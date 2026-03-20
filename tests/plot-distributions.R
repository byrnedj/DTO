#!/usr/bin/env Rscript
# Plot baseline vs current latency distributions from perf_combined output.
#
# Usage: Rscript tests/plot-distributions.R [results_dir] [pagesize]
#   results_dir - directory with distributions_*.csv (default: build/perf_results)
#   pagesize    - "4k" or "2m" (default: "4k")
#
# Generates:
#   distributions_{pagesize}_density.png  - overlaid density curves
#   distributions_{pagesize}_violin.png   - violin plots per test/config
#   distributions_{pagesize}_ecdf.png     - empirical CDF (most sensitive)

library(ggplot2)
library(dplyr)

args <- commandArgs(trailingOnly = TRUE)
results_dir <- if (length(args) >= 1) args[1] else "build/perf_results"
pagesize    <- if (length(args) >= 2) args[2] else "4k"
optype    <- if (length(args) >= 3) args[3] else "memcpy"

csv_path <- file.path(results_dir, "distributions.csv")
if (!file.exists(csv_path)) {
  stop("CSV not found: ", csv_path,
       "\nRun: ctest -R perf_combined --output-on-failure")
}

d <- read.csv(csv_path, stringsAsFactors = FALSE)

# Filter to requested pagesize and op type
d <- d[d$pagesize == pagesize, ]
d <- d[grepl(paste("^",optype,sep=""), d$test), ]
if (nrow(d) == 0) {
  stop("No data for pagesize '", pagesize, "' in ", csv_path)
}
cat("Loaded", nrow(d), "samples from", csv_path, "\n")

# Trim outliers per group (keep 1st-99th percentile) for cleaner plots
d <- d %>%
  group_by(test, config, version) %>%
  filter(latency_ns >= quantile(latency_ns, 0.01),
         latency_ns <= quantile(latency_ns, 0.99)) %>%
  ungroup()

d$version <- factor(d$version, levels = c("baseline", "current"))
d$config  <- factor(d$config, levels = c("cpu", "stdc", "dsa", "dsa_auto"))

# Compute median per group for annotation
medians <- d %>%
  group_by(test, config, version) %>%
  summarise(med = median(latency_ns), .groups = "drop")

# --- 1. Density plot: overlaid distributions ---
p1 <- ggplot(d, aes(x = latency_ns, fill = version, color = version)) +
  geom_density(alpha = 0.3, linewidth = 0.5) +
  geom_vline(data = medians, aes(xintercept = med, color = version),
             linetype = "dashed", linewidth = 0.4) +
  facet_wrap(~ test + config, scales = "free", ncol = 3,
             labeller = labeller(.multi_line = FALSE)) +
  scale_fill_manual(values = c(baseline = "#2166ac", current = "#b2182b")) +
  scale_color_manual(values = c(baseline = "#2166ac", current = "#b2182b")) +
  labs(title = paste("Latency Distributions: Baseline vs Current (", pagesize, ",", optype, ")"),
       x = "Latency (ns)", y = "Density",
       fill = "Version", color = "Version") +
  theme_minimal(base_size = 10) +
  theme(strip.text = element_text(face = "bold", size = 7),
        legend.position = "bottom",
        axis.text.x = element_text(angle = 45, hjust = 1))

outfile <- file.path(results_dir,
                     paste0("distributions_", pagesize, "_", optype, "_density.png"))
ggsave(outfile, p1, width = 16, height = 20, dpi = 150)
cat("Saved", outfile, "\n")

# --- 2. Violin plot: shape comparison ---
p2 <- ggplot(d, aes(x = version, y = latency_ns, fill = version)) +
  geom_violin(alpha = 0.6, draw_quantiles = c(0.25, 0.5, 0.75)) +
  facet_wrap(~ test + config, scales = "free_y", ncol = 3,
             labeller = labeller(.multi_line = FALSE)) +
  scale_fill_manual(values = c(baseline = "#2166ac", current = "#b2182b")) +
  labs(title = paste("Latency Violins: Baseline vs Current (", pagesize, ",", optype, ")"),
       x = "", y = "Latency (ns)") +
  theme_minimal(base_size = 10) +
  theme(strip.text = element_text(face = "bold", size = 7),
        legend.position = "none")

outfile <- file.path(results_dir,
                     paste0("distributions_", pagesize, "_", optype, "_violin.png"))
ggsave(outfile, p2, width = 16, height = 20, dpi = 150)
cat("Saved", outfile, "\n")

# --- 3. ECDF plot: most sensitive to distribution shifts ---
p3 <- ggplot(d, aes(x = latency_ns, color = version)) +
  stat_ecdf(linewidth = 0.5) +
  facet_wrap(~ test + config, scales = "free_x", ncol = 3,
             labeller = labeller(.multi_line = FALSE)) +
  scale_color_manual(values = c(baseline = "#2166ac", current = "#b2182b")) +
  labs(title = paste("Empirical CDF: Baseline vs Current (", pagesize, "," , optype, ")"),
       x = "Latency (ns)", y = "Cumulative Probability",
       color = "Version") +
  theme_minimal(base_size = 10) +
  theme(strip.text = element_text(face = "bold", size = 7),
        legend.position = "bottom",
        axis.text.x = element_text(angle = 45, hjust = 1))

outfile <- file.path(results_dir,
                     paste0("distributions_", pagesize, "_", optype, "_ecdf.png"))
ggsave(outfile, p3, width = 16, height = 20, dpi = 150)
cat("Saved", outfile, "\n")

cat("\nDone. All plots in", results_dir, "\n")
