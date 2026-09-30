import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  output: "export",
  basePath: "/OpenCubeOS",
  assetPrefix: "/OpenCubeOS/",
  images: { unoptimized: true },
};

export default nextConfig;
