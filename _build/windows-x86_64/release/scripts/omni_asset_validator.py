import os, sys
import argparse
import asyncio
import inspect
import traceback

class __ArgFormatter(argparse.RawTextHelpFormatter, argparse.ArgumentDefaultsHelpFormatter):
    pass

def main():

    try:
        # Python 3.8 - PATH no longer works, use this to add DLL search paths
        if hasattr(os, "add_dll_directory"):
            os.add_dll_directory(os.environ["CARB_APP_PATH"])
        import carb
    except ModuleNotFoundError:
        print(
            "Unable to import carb module. Did you configure your PYTHONPATH correctly? "
            f"See traceback for details:\n{traceback.format_exc()}"
        )
        return 1

    carb.get_framework().startup([])

    import omni.log
    log = omni.log.get_log()
    log.set_channel_enabled("omni_asset_validator", True, omni.log.SettingBehavior.OVERRIDE)
    log.set_channel_level("omni_asset_validator", omni.log.Level.VERBOSE, omni.log.SettingBehavior.OVERRIDE)
    omni.log.info(f"Running in {os.getcwd()}", channel="omni_asset_validator")

    try:
        import omni.asset_validator.core
    except ModuleNotFoundError:
        omni.log.fatal(
            "Unable to import omni.asset_validator.core module. Did you configure your PYTHONPATH correctly? "
            f"See traceback for details:\n{traceback.format_exc()}",
            channel="omni_asset_validator"
        )
        return 1

    parser = argparse.ArgumentParser()
    parser.prog = "omni_asset_validator"
    parser.formatter_class = __ArgFormatter
    parser.description = inspect.cleandoc(
        """
        Utility for USD validation to ensure layers run smoothly across all Omniverse
        products. Validation is based on the USD ComplianceChecker (i.e. the same
        backend as the usdchecker commandline tool), and has been extended with
        additional rules as follows:

            - Additional "Basic" rules applicable in the broader USD ecosystem.
            - Omniverse centric rules that ensure layer files work well with all
              Omniverse applications & connectors.
            - Configurable end-user rules that can be specific to individual company
              and/or team workflows.
                > Note this level of configuration requires manipulating PYTHONPATH
                  prior to launching this tool.
        """
    )
    parser.epilog = "See https://tinyurl.com/omni-asset-validator for more details."

    parser.add_argument(
        "asset",
        metavar="URI",
        type=str,
        nargs="?",
        help=inspect.cleandoc(
            """
            A single Omniverse Asset.
              > Note: This can be a file URI or folder/container URI.
            """
        ),
    )

    defaultRulesArg = parser.add_argument(
        "-d",
        "--defaultRules",
        metavar="0|1",
        required=False,
        type=int,
        default=1,
        help=inspect.cleandoc(
            """
            Flag to use the default-enabled validation rules.
            Opt-out of this behavior to gain finer control over
            the rules using the --categories and --rules flags.
            """
        ),
    )

    categoriesArg = parser.add_argument(
        "-c",
        "--categories",
        metavar="CATEGORY",
        required=False,
        type=str,
        default=[],
        nargs='+',
        help="Categories to enable, regardless of the --defaultRules flag."
    )

    rulesArg = parser.add_argument(
        "-r",
        "--rules",
        metavar="RULE",
        required=False,
        type=str,
        default=[],
        nargs='+',
        help="Rules to enable, regardless of the --defaultRules flag."
    )

    parser.add_argument(
        "-e",
        "--explain",
        required=False,
        action='store_true',
        help=inspect.cleandoc(
            """
            Rather than running the validator, provide descriptions
            for each configured rule.
            """
        ),
    )

    allCategories = omni.asset_validator.core.ValidationRulesRegistry.categories()
    categoriesArg.help += f"\nValid categories are:\n  " + "\n  ".join(allCategories)
    categoriesArg.help +="\n"

    rulesArg.help += "\nValid rules include:"
    for category in allCategories:
        for rule in omni.asset_validator.core.ValidationRulesRegistry.rules(category):
            rulesArg.help += f"\n  {rule.__name__}"
    rulesArg.help += "\n"

    defaultRulesArg.help += "\nThe default configuration includes:"
    for category in omni.asset_validator.core.ValidationRulesRegistry.categories(enabledOnly=True):
        for rule in omni.asset_validator.core.ValidationRulesRegistry.rules(category, enabledOnly=True):
            defaultRulesArg.help += f"\n  {rule.__name__}"
    defaultRulesArg.help += "\n"

    args = parser.parse_args()

    def __logRule(rule: omni.asset_validator.core.BaseRuleChecker):
        omni.log.info(f'{rule.__name__} : {rule.GetDescription()}\n', channel="omni_asset_validator")

    engine = omni.asset_validator.core.ValidationEngine(initRules=args.defaultRules)

    if not args.explain and not args.asset:
        omni.log.fatal("An Asset URI must be specified", channel="omni_asset_validator")
        return 1

    if args.explain and args.defaultRules:
        for category in omni.asset_validator.core.ValidationRulesRegistry.categories(enabledOnly=True):
            for rule in omni.asset_validator.core.ValidationRulesRegistry.rules(category, enabledOnly=True):
                __logRule(rule)

    for category in args.categories:
        for rule in omni.asset_validator.core.ValidationRulesRegistry.rules(category):
            if args.explain:
                __logRule(rule)
            else:
                engine.enableRule(rule)

    for r in args.rules:
        rule = omni.asset_validator.core.ValidationRulesRegistry.rule(r)
        if args.explain:
            __logRule(rule)
        else:
            engine.enableRule(rule)

    if args.explain:
        return 0

    omni.log.info(f"Validating: {args.asset}", channel="omni_asset_validator")

    def __logResults(results: omni.asset_validator.core.Results):
        omni.log.info(f"Results for Asset '{results.asset}'", channel="omni_asset_validator")
        for issue in results.issues(omni.asset_validator.core.IssuePredicates.IsError()):
            omni.log.fatal(str(issue), channel="omni_asset_validator")
        for issue in results.issues(omni.asset_validator.core.IssuePredicates.IsFailure()):
            omni.log.error(str(issue), channel="omni_asset_validator")
        for issue in results.issues(omni.asset_validator.core.IssuePredicates.IsWarning()):
            omni.log.warn(str(issue), channel="omni_asset_validator")

    async def __validate(engine: omni.asset_validator.core.ValidationEngine, uri: str):
        results = await engine.validate_async(uri)
        for result in results:
            __logResults(result)

    asyncio.run(__validate(engine, args.asset))

    return 0

if __name__ == '__main__':
    sys.exit(main())
