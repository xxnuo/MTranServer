const { preloadModel, supportedLanguages, shutdown } = require("./translator");

// 如果直接执行此脚本，则从命令行参数获取语言对并预加载
async function main() {
  if (require.main === module) {
    const args = process.argv.slice(2);
    if (args.length !== 2) {
      console.log("Usage: node preload.js <from> <to>");
      console.log("Available languages:", supportedLanguages.join(", "));
      process.exit(1);
    }

    const [from, to] = args;
    try {
      console.log(`Preloading model for ${from} to ${to}...`);
      await preloadModel(from, to);
      console.log("Preload completed successfully.");
      await shutdown();
    } catch (error) {
      console.error("Error during preload:", error.message);
      process.exit(1);
    }
  }
}

main();
