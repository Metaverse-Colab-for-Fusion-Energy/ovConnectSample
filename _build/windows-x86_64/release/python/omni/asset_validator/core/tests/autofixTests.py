import pathlib
from contextlib import contextmanager
from unittest.mock import Mock

import omni.kit.test

from omni.asset_validator.core import (
    Issue,
    IssueSeverity,
    IssueFixer,
    FixStatus,
    BaseRuleChecker,
    Identifier,
    to_identifier,
    ValidationEngine,
    IssuePredicates,
    Results,
    FixResult,
    Suggestion,
)
from pxr import Usd, Sdf
from .engineTests import getUrl


class MyRuleChecker(BaseRuleChecker):

    @staticmethod
    def GetDescription():
        return "Check that all active prims are meshes for xforms"

    def CheckPrim(self, prim) -> None:
        self._Msg("Checking prim <%s>." % prim.GetPath())
        if prim.GetTypeName() not in ("Mesh", "Xform") and prim.IsActive():
            self._AddFailedCheck(
                message=f"Prim <{prim.GetPath()}> has unsupported type '{prim.GetTypeName()}'.",
                at=prim,
                suggestion=Suggestion(
                    lambda _, _prim: _prim.SetActive(False),
                    "Disable Prim",
                )
            )


class IdentifierTests(omni.kit.test.AsyncTestCase):

    async def test_stage_id(self):
        # Given / When
        def _create_identifer() -> Identifier[Usd.Stage]:
            stage = Usd.Stage.Open(getUrl("helloworld.usda"))
            return to_identifier(stage)

        # Then
        stage_ref = _create_identifer()
        stage = Usd.Stage.Open(getUrl("helloworld.usda"))
        self.assertTrue(stage is stage_ref.restore(stage))

    async def test_prim_id(self):
        # Given / When
        def _create_identifer() -> Identifier[Usd.Prim]:
            stage = Usd.Stage.Open(getUrl("helloworld.usda"))
            prim = stage.GetPrimAtPath("/World/cube")
            return to_identifier(prim)

        # Then
        prim_ref = _create_identifer()
        stage = Usd.Stage.Open(getUrl("helloworld.usda"))
        prim = stage.GetPrimAtPath("/World/cube")
        self.assertTrue(prim == prim_ref.restore(stage))

    async def test_attribute_id(self):
        # Given / When
        def _create_identifer() -> Identifier[Usd.Attribute]:
            stage = Usd.Stage.Open(getUrl("helloworld.usda"))
            prim = stage.GetPrimAtPath("/World/cube")
            attr = prim.GetAttribute("size")
            return to_identifier(attr)

        # Then
        attr_ref = _create_identifer()
        stage = Usd.Stage.Open(getUrl("helloworld.usda"))
        prim = stage.GetPrimAtPath("/World/cube")
        attr = prim.GetAttribute("size")
        self.assertTrue(attr == attr_ref.restore(stage))


class IssueTests(omni.kit.test.AsyncTestCase):

    async def test_issue_construct_rule(self):
        # Given
        class Rule:
            pass

        # When / Then
        with self.assertRaises(ValueError):
            Issue(message="message", severity=IssueSeverity.ERROR, rule=Rule)

    async def test_issue_none_str(self):
        # Given / When
        issue = Issue.none()
        # Then
        self.assertEquals(str(issue), "No Issue found.")

    async def test_issue_error_str(self):
        # Given / When
        issue = Issue(
            message="A message",
            severity=IssueSeverity.ERROR,
            rule=MyRuleChecker,
        )
        # Then
        self.assertEquals(str(issue), "Error checking rule MyRuleChecker: A message.")

    async def test_issue_error_str_at_fix(self):
        # Given
        at = Mock(spec=Usd.Prim)
        at.GetPrimPath.return_value = "/World"
        at.GetStage.return_value.GetRootLayer.return_value.identifier = "@stage@"
        suggestion = Suggestion(message="suggestion", callable=lambda a, b: None)
        # When
        issue = Issue(
            message="A message",
            severity=IssueSeverity.ERROR,
            rule=MyRuleChecker,
            at=at,
            suggestion=suggestion,
        )
        # Then
        self.assertEquals(
            str(issue),
            "Error checking rule MyRuleChecker: A message. At Prim </World>. Suggestion: suggestion."
        )

    async def test_issue_warning_str(self):
        # Given / When
        issue = Issue(
            message="A message",
            severity=IssueSeverity.WARNING,
            rule=MyRuleChecker,
        )
        # Then
        self.assertEquals(str(issue), "Warning checking rule MyRuleChecker: A message.")

    async def test_issue_warning_str_at_fix(self):
        # Given
        at = Mock(spec=Usd.Prim)
        at.GetPrimPath.return_value = "/World"
        at.GetStage.return_value.GetRootLayer.return_value.identifier = "@stage@"
        suggestion = Suggestion(message="suggestion", callable=lambda a, b: None)
        # When
        issue = Issue(
            message="A message",
            severity=IssueSeverity.WARNING,
            rule=MyRuleChecker,
            at=at,
            suggestion=suggestion,
        )
        # Then
        self.assertEquals(
            str(issue),
            "Warning checking rule MyRuleChecker: A message. At Prim </World>. Suggestion: suggestion."
        )

    async def test_issue_failure_str(self):
        # Given / When
        issue = Issue(
            message="A message",
            severity=IssueSeverity.FAILURE,
            rule=MyRuleChecker,
        )
        # Then
        self.assertEquals(str(issue), "Failure checking rule MyRuleChecker: A message.")

    async def test_issue_failure_str_at_fix(self):
        # Given
        at = Mock(spec=Usd.Prim)
        at.GetPrimPath.return_value = "/World"
        at.GetStage.return_value.GetRootLayer.return_value.identifier = "@stage@"
        suggestion = Suggestion(message="suggestion", callable=lambda a, b: None)
        # When
        issue = Issue(
            message="A message",
            severity=IssueSeverity.FAILURE,
            rule=MyRuleChecker,
            at=at,
            suggestion=suggestion,
        )
        # Then
        self.assertEquals(
            str(issue),
            "Failure checking rule MyRuleChecker: A message. At Prim </World>. Suggestion: suggestion."
        )


class IssueFixerTests(omni.kit.test.AsyncTestCase):

    async def setUp(self) -> None:
        super(IssueFixerTests, self).setUp()
        self.stage = Mock(spec=Usd.Stage)
        self.layer = Mock(spec=Sdf.Layer)
        self.layer.identifier = getUrl("helloworld.usda")
        self.stage.GetEditTarget.return_value.GetLayer.return_value = self.layer
        self.stage.GetRootLayer.return_value = self.layer
        self.issue = Mock(spec=Issue)
        self.issue.at = Mock(spec=Identifier)

    async def test_init_fails(self):
        # Given
        prim = Mock(spec=Usd.Prim)
        # When / Then
        with self.assertRaises(ValueError):
            IssueFixer(prim)

    async def test_fix_success(self):
        # Given / When
        fixer = IssueFixer(self.stage)
        status = fixer.fix([self.issue])
        fixer.save()
        # Then
        self.assertEquals(status, [FixResult(self.issue, FixStatus.SUCCESS)])
        self.issue.suggestion.assert_called_with(self.stage, self.issue.at.restore.return_value)
        self.stage.Save.assert_called_with()

    async def test_fix_failure(self):
        # Given
        exception = ValueError()
        self.issue.suggestion.side_effect = exception
        # When
        fixer = IssueFixer(self.stage)
        status = fixer.fix([self.issue])
        fixer.save()
        # Then
        self.assertEquals(status, [FixResult(self.issue, FixStatus.FAILURE, exception)])
        self.issue.suggestion.assert_called_with(self.stage, self.issue.at.restore.return_value)
        self.stage.Save.assert_called_with()

    async def test_fix_io_error(self):
        # Given
        self.layer.identifier = "notfound.usda"
        # When
        fixer = IssueFixer(self.stage)
        status = fixer.fix([self.issue])
        with self.assertRaises(IOError):
            fixer.save()
        # Then
        self.assertEquals(status, [FixResult(self.issue, FixStatus.SUCCESS)])
        self.issue.suggestion.assert_called_with(self.stage, self.issue.at.restore.return_value)
        self.stage.Save.assert_not_called()

    async def test_fix_at(self):
        # Given
        stage = Usd.Stage.Open(getUrl("helloworld.usda"))
        session_layer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        stage.GetSessionLayer().subLayerPaths.append(session_layer.identifier)
        # When
        fixer = IssueFixer(stage)
        status = fixer.fix_at([self.issue], session_layer)
        with self.assertRaises(IOError):
            fixer.save()
        # Then
        self.assertEquals(status, [FixResult(self.issue, FixStatus.SUCCESS)])
        self.issue.suggestion.assert_called_with(stage, self.issue.at.restore.return_value)
        self.stage.Save.assert_not_called()


class BaseRuleCheckerTest(omni.kit.test.AsyncTestCase):
    """
    Tests modifications to BaseRuleChecker.
    """

    async def setUp(self) -> None:
        super(BaseRuleCheckerTest, self).setUp()
        self.message = "A message"
        self.suggestion = Mock(spec=Suggestion)
        stage = Mock(spec=Usd.Stage)
        stage.GetRootLayer.return_value.identifier = "/path/usd.usd"
        self.prim = Mock(spec=Usd.Prim)
        self.prim.GetStage.return_value = stage
        self.prim.GetPrimPath.return_value = "/World/cube"

    async def test_add_failed_check_legacy(self):
        # Given / When
        checker = MyRuleChecker(False, False, False)
        checker._AddFailedCheck(
            message=self.message,
        )
        # Then
        self.assertEquals(checker.GetFailedChecks(),
                          [Issue.from_(IssueSeverity.FAILURE, MyRuleChecker, self.message)])

    async def test_add_error_legacy(self):
        # Given / When
        checker = MyRuleChecker(False, False, False)
        checker._AddError(
            message=self.message,
        )
        # Then
        self.assertEquals(checker.GetErrors(),
                          [Issue.from_(IssueSeverity.ERROR, MyRuleChecker, self.message)])

    async def test_add_warning_legacy(self):
        # Given / When
        checker = MyRuleChecker(False, False, False)
        checker._AddWarning(
            message=self.message,
        )
        # Then
        self.assertEquals(checker.GetWarnings(),
                          [Issue.from_(IssueSeverity.WARNING, MyRuleChecker, self.message)])

    async def test_add_failed_check(self):
        # Given / When
        checker = MyRuleChecker(False, False, False)
        checker._AddFailedCheck(
            message=self.message,
            at=self.prim,
            suggestion=self.suggestion,
        )
        # Then
        self.assertEquals(checker.GetFailedChecks(),
                          [omni.asset_validator.core.Issue(
                              message=self.message,
                              severity=IssueSeverity.FAILURE,
                              rule=MyRuleChecker,
                              at=self.prim,
                              suggestion=self.suggestion,
                          )])

    async def test_add_error_check(self):
        # Given / When
        checker = MyRuleChecker(False, False, False)
        checker._AddError(
            message=self.message,
            at=self.prim,
            suggestion=self.suggestion,
        )
        # Then
        self.assertEquals(checker.GetErrors(),
                          [omni.asset_validator.core.Issue(
                              message=self.message,
                              severity=IssueSeverity.ERROR,
                              rule=MyRuleChecker,
                              at=self.prim,
                              suggestion=self.suggestion,
                          )])

    async def test_add_warning_check(self):
        # Given / When
        checker = MyRuleChecker(False, False, False)
        checker._AddWarning(
            message=self.message,
            at=self.prim,
            suggestion=self.suggestion,
        )
        # Then
        self.assertEquals(checker.GetWarnings(),
                          [omni.asset_validator.core.Issue(
                              message=self.message,
                              severity=IssueSeverity.WARNING,
                              rule=MyRuleChecker,
                              at=self.prim,
                              suggestion=self.suggestion,
                          )])


class ResultsTest(omni.kit.test.AsyncTestCase):
    def test_str(self):
        # Given
        result = Results(
            asset="Any asset",
            issues=[Issue.from_(IssueSeverity.FAILURE, MyRuleChecker, "failure")]
        )
        # When
        actual = str(result)
        # Then
        self.assertEquals(
            actual,
"""Results(
    asset="Any asset",
    issues=[
        Issue(
            message="failure",
            severity=IssueSeverity.FAILURE,
            rule=MyRuleChecker,
            at=None,
            suggestion=None
        )
    ]
)"""
        )


class AutoFixTest(omni.kit.test.AsyncTestCase):
    """
    Showcases of auto fix.
    """

    @contextmanager
    def rollback(self, path) -> pathlib.Path:
        file = pathlib.Path(path)
        old_content = file.read_text()
        try:
            yield file
        finally:
            file.write_text(old_content)

    async def test_commit_fix_url(self):
        url: str = getUrl("autofixPrev.usda")
        engine = ValidationEngine(initRules=False)
        engine.enableRule(MyRuleChecker)

        result = engine.validate(url)
        issues = result.issues(
            IssuePredicates.And(
                IssuePredicates.IsFailure(),
                IssuePredicates.IsRule(MyRuleChecker)
            )
        )

        with self.rollback(url) as path:
            fixer = IssueFixer(url)
            fixer.fix(issues)
            fixer.save()
            self.assertEquals(
                path.read_text(),
                pathlib.Path(getUrl("autofixNext.usda")).read_text()
            )
