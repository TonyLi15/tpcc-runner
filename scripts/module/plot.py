import matplotlib.pyplot as plt
import matplotlib.ticker as ptick  ##これが必要！


class Plot:
    markersize = 20

    def __init__(self, VARYING_TYPE, x_label, CLOCKS_PER_US, exp_param):
        self.VARYING_TYPE = VARYING_TYPE
        self.x_label = x_label
        self.clocks = {
            "CLOCKS_PER_NS": (CLOCKS_PER_US / 1000),
            "CLOCKS_PER_US": CLOCKS_PER_US,
            "CLOCKS_PER_MS": (CLOCKS_PER_US * 1000),
            "CLOCKS_PER_S": (CLOCKS_PER_US * 1000 * 1000),
        }
        self.clocks_label = {
            "CLOCKS_PER_NS": "[ns]",
            "CLOCKS_PER_US": "[μs]",
            "CLOCKS_PER_MS": "[ms]",
            "CLOCKS_PER_S": "[s]",
        }
        self.exp_param = exp_param

    def get_x_ticks(self, df, column):
        return [str(req) for req in df[column]]

    def plot_tps(self, df):
        fig, ax1 = plt.subplots(dpi=300)

        ax1.plot(
            self.get_x_ticks(df, self.VARYING_TYPE),
            df["num_commits"],
            markerfacecolor="None",
            markersize=self.markersize,
            clip_on=False,
        )

        fig.tight_layout()
        ax1.set_ylabel("Throughput [TPS]", fontsize=25)
        ax1.yaxis.set_major_formatter(
            ptick.ScalarFormatter(useMathText=True)
        )  # こっちを先に書くこと。
        ax1.yaxis.get_offset_text().set_fontsize(19)
        plt.tick_params(labelsize=19)
        plt.legend(fontsize=19)
        plt.xticks(rotation=40)
        plt.xlabel(self.x_label[self.VARYING_TYPE], fontsize=25)

        plt.grid()
        plt.savefig(
            "tps_" + "_varying_" + self.VARYING_TYPE + ".pdf",
            bbox_inches="tight",
        )
        plt.savefig(
            "tps_" + "_varying_" + self.VARYING_TYPE + ".eps",
            bbox_inches="tight",
        )

    def plot_avg_latency(self, df):
        fig, ax1 = plt.subplots(dpi=300)

        ax1.plot(
            self.get_x_ticks(df, self.VARYING_TYPE),
            df["total_latency"] / df["num_commits"] / self.clocks["CLOCKS_PER_MS"],
            markerfacecolor="None",
            markersize=self.markersize,
            clip_on=False,
        )
        fig.tight_layout()

        ax1.set_ylabel(
            "Latency" + self.clocks_label["CLOCKS_PER_US"],
            fontsize=18,
        )

        plt.legend(fontsize=19)
        plt.tick_params(labelsize=19)
        plt.xticks(rotation=40)
        plt.xlabel(self.x_label[self.VARYING_TYPE], fontsize=25)

        ax = plt.gca()  # get current axes 現在の軸設定データを取得する
        tick_spacing = 10  # 目盛り表示する間隔
        ax.yaxis.set_major_locator(
            ptick.MaxNLocator(tick_spacing)
        )  # X軸目盛の表示間隔を間引く
        _, y_max = ax.get_ylim()
        ax.set_ylim(0, y_max)

        plt.grid()
        plt.savefig(
            "latency_varying_" + self.VARYING_TYPE + ".pdf",
            bbox_inches="tight",
        )
        plt.savefig(
            "latency_varying_" + self.VARYING_TYPE + ".eps",
            bbox_inches="tight",
        )
