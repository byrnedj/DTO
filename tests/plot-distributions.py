#!/usr/bin/env python3
# Plot baseline vs current latency distributions from perf output.
#
# Python port of plot-distributions.R (identical inputs, outputs, and file
# names). Requires: pandas, numpy, matplotlib, scipy.
#   pip install pandas numpy matplotlib scipy
#
# Usage: python3 tests/plot-distributions.py [results_dir] [pagesize] [optype]
#   results_dir - directory with distributions.csv (default: build/perf_results)
#   pagesize    - "4k" or "2m" (default: "4k")
#   optype      - "memcpy", "memset", or "memcmp" (default: "memcpy")
#
# Generates:
#   distributions_{pagesize}_{optype}_density.png  - overlaid density curves
#   distributions_{pagesize}_{optype}_violin.png   - violin plots per test/config
#   distributions_{pagesize}_{optype}_ecdf.png     - empirical CDF (most sensitive)

import os
import sys

import numpy as np
import pandas as pd
from scipy.stats import gaussian_kde

import matplotlib
matplotlib.use("Agg")  # headless / CI-safe
import matplotlib.pyplot as plt

# Match the R script's palette and category ordering.
COLORS = {"baseline": "#2166ac", "current": "#b2182b"}
VERSION_ORDER = ["baseline", "current"]
CONFIG_ORDER = ["cpu", "stdc", "dsa", "dsa_auto"]
NCOL = 3


def main():
    results_dir = sys.argv[1] if len(sys.argv) >= 2 else "build/perf_results"
    pagesize = sys.argv[2] if len(sys.argv) >= 3 else "4k"
    optype = sys.argv[3] if len(sys.argv) >= 4 else "memcpy"

    csv_path = os.path.join(results_dir, "distributions.csv")
    if not os.path.isfile(csv_path):
        sys.exit("CSV not found: {}\nRun: ctest -R perf "
                 "--output-on-failure".format(csv_path))

    d = pd.read_csv(csv_path)

    # Filter to requested pagesize and op type.
    d = d[d["pagesize"] == pagesize]
    d = d[d["test"].str.startswith(optype)]
    if len(d) == 0:
        sys.exit("No data for pagesize '{}' in {}".format(pagesize, csv_path))
    print("Loaded {} samples from {}".format(len(d), csv_path))

    # Trim outliers per group (keep 1st-99th percentile) for cleaner plots.
    grp = d.groupby(["test", "config", "version"])["latency_ns"]
    lo = grp.transform(lambda x: x.quantile(0.01))
    hi = grp.transform(lambda x: x.quantile(0.99))
    d = d[(d["latency_ns"] >= lo) & (d["latency_ns"] <= hi)].reset_index(drop=True)

    # Deterministic facet order: by test (as encountered), then config order.
    tests = list(dict.fromkeys(d["test"]))
    facets = [(t, c) for t in tests for c in CONFIG_ORDER
              if not d[(d["test"] == t) & (d["config"] == c)].empty]

    _plot_density(d, facets, results_dir, pagesize, optype)
    _plot_violin(d, facets, results_dir, pagesize, optype)
    _plot_ecdf(d, facets, results_dir, pagesize, optype)

    print("\nDone. All plots in {}".format(results_dir))


def _make_grid(n):
    nrow = (n + NCOL - 1) // NCOL
    fig, axes = plt.subplots(nrow, NCOL, figsize=(16, 20), squeeze=False)
    flat = [ax for row in axes for ax in row]
    for ax in flat[n:]:          # hide unused cells
        ax.axis("off")
    return fig, flat


def _group(d, test, config, version):
    return d[(d["test"] == test) & (d["config"] == config)
             & (d["version"] == version)]["latency_ns"].to_numpy()


def _save(fig, results_dir, pagesize, optype, kind):
    # Reserve a strip at top for the suptitle and at bottom for the legend.
    fig.tight_layout(rect=(0, 0.03, 1, 0.98))
    outfile = os.path.join(
        results_dir,
        "distributions_{}_{}_{}.png".format(pagesize, optype, kind))
    fig.savefig(outfile, dpi=150)
    plt.close(fig)
    print("Saved {}".format(outfile))


def _plot_density(d, facets, results_dir, pagesize, optype):
    fig, axes = _make_grid(len(facets))
    for ax, (test, config) in zip(axes, facets):
        for version in VERSION_ORDER:
            vals = _group(d, test, config, version)
            if len(vals) == 0:
                continue
            color = COLORS[version]
            if len(np.unique(vals)) >= 2:
                kde = gaussian_kde(vals)
                xs = np.linspace(vals.min(), vals.max(), 512)
                ys = kde(xs)
                ax.fill_between(xs, ys, alpha=0.3, color=color)
                ax.plot(xs, ys, color=color, linewidth=0.5, label=version)
            # dashed median line
            ax.axvline(np.median(vals), color=color, linestyle="--",
                       linewidth=0.4)
        ax.set_title("{} {}".format(test, config), fontsize=7,
                     fontweight="bold")
        ax.set_xlabel("Latency (ns)", fontsize=7)
        ax.set_ylabel("Density", fontsize=7)
        ax.tick_params(axis="x", labelrotation=45, labelsize=6)
        ax.tick_params(axis="y", labelsize=6)
    _legend(fig)
    fig.suptitle("Latency Distributions: Baseline vs Current "
                 "( {} , {} )".format(pagesize, optype), fontsize=12)
    _save(fig, results_dir, pagesize, optype, "density")


def _plot_violin(d, facets, results_dir, pagesize, optype):
    fig, axes = _make_grid(len(facets))
    for ax, (test, config) in zip(axes, facets):
        data, positions, colors = [], [], []
        for i, version in enumerate(VERSION_ORDER, start=1):
            vals = _group(d, test, config, version)
            if len(vals) == 0:
                continue
            data.append(vals)
            positions.append(i)
            colors.append(COLORS[version])
        if data:
            parts = ax.violinplot(data, positions=positions, showextrema=False,
                                  quantiles=[[0.25, 0.5, 0.75]] * len(data))
            for body, color in zip(parts["bodies"], colors):
                body.set_facecolor(color)
                body.set_alpha(0.6)
            if "cquantiles" in parts:
                parts["cquantiles"].set_color("black")
                parts["cquantiles"].set_linewidth(0.6)
        ax.set_xticks([1, 2])
        ax.set_xticklabels(VERSION_ORDER, fontsize=7)
        ax.set_title("{} {}".format(test, config), fontsize=7,
                     fontweight="bold")
        ax.set_ylabel("Latency (ns)", fontsize=7)
        ax.tick_params(axis="y", labelsize=6)
    fig.suptitle("Latency Violins: Baseline vs Current "
                 "( {} , {} )".format(pagesize, optype), fontsize=12)
    _save(fig, results_dir, pagesize, optype, "violin")


def _plot_ecdf(d, facets, results_dir, pagesize, optype):
    fig, axes = _make_grid(len(facets))
    for ax, (test, config) in zip(axes, facets):
        for version in VERSION_ORDER:
            vals = _group(d, test, config, version)
            if len(vals) == 0:
                continue
            xs = np.sort(vals)
            ys = np.arange(1, len(xs) + 1) / len(xs)
            ax.step(xs, ys, where="post", color=COLORS[version],
                    linewidth=0.5, label=version)
        ax.set_title("{} {}".format(test, config), fontsize=7,
                     fontweight="bold")
        ax.set_xlabel("Latency (ns)", fontsize=7)
        ax.set_ylabel("Cumulative Probability", fontsize=7)
        ax.tick_params(axis="x", labelrotation=45, labelsize=6)
        ax.tick_params(axis="y", labelsize=6)
    _legend(fig)
    fig.suptitle("Empirical CDF: Baseline vs Current "
                 "( {} , {} )".format(pagesize, optype), fontsize=12)
    _save(fig, results_dir, pagesize, optype, "ecdf")


def _legend(fig):
    handles = [plt.Line2D([0], [0], color=COLORS[v], lw=2, label=v)
               for v in VERSION_ORDER]
    fig.legend(handles=handles, loc="lower center", ncol=2,
               title="Version", frameon=False, bbox_to_anchor=(0.5, 0.0))


if __name__ == "__main__":
    main()
