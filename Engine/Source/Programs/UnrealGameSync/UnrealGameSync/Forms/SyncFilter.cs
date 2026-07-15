// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;
using UnrealGameSync.Forms;

namespace UnrealGameSync
{
	partial class SyncFilter : ThemedForm
	{
		private class PresetItem
		{
			public string Name { get; set; } = String.Empty;

			public string Suffix { get; set; } = String.Empty;

			public override string ToString()
			{
				if (String.IsNullOrWhiteSpace(Suffix))
				{
					return Name;
				}

				return $"{Name} {Suffix}";
			}
		}

		private class CheckListItem
		{
			public string Name => GetName();
			public Guid UniqueId { get; set; } = Guid.Empty;
			public string CategoryName { get; set; } = String.Empty;
			public bool Locked { get; set; } = false;
			public string Preset { get; set; } = String.Empty;
			public bool Enabled { get; set; } = false;
			public bool IsReadOnly { get; set; } = false;
			public List<string> Paths { get; set; } = new List<string>();
			public string ToolTip { get; set; } = String.Empty;

			public override string ToString()
			{
				return GetName();
			}

			private string GetName()
			{
				if (String.IsNullOrWhiteSpace(Preset))
				{
					if (IsReadOnly)
					{
						return $"{CategoryName} (Disabled)";
					}

					return CategoryName;
				}

				string suffix = Locked ? " - Locked" : String.Empty;
				return $"{CategoryName} [Preset: {Preset}{suffix}]";
			}
		}

		private readonly UserWorkspaceSettings _userWorkspaceSettings;
		private readonly UserProjectSettings _userProjectSettings;
		private readonly Dictionary<Guid, WorkspaceSyncCategory> _uniqueIdToCategory;
		private readonly string _currentPreset;
		private readonly string _defaultPreset;
		private readonly bool _forceDefaultPreset;
		private readonly IDictionary<string, Preset> _availablePresets;
		private readonly ConfigSection? _perforceSection;
		private readonly ISet<string> _baseContentPaths;

		public FilterSettings GlobalFilter;
		public FilterSettings WorkspaceFilter;
		
		public SyncFilter(
			UserWorkspaceSettings userWorkspaceSettings,
			UserProjectSettings userProjectSettings,
			Dictionary<Guid, WorkspaceSyncCategory> uniqueIdToCategory,
			string roleName,
			string defaultPreset,
			bool forceDefaultPreset,
			IDictionary<string, Preset> roles,
			ISet<string> baseContentPaths,
			FilterSettings globalFilter,
			FilterSettings workspaceFilter,
			ConfigSection? perforceSection,
			bool defaultToWorkspaceView)
		{
			InitializeComponent(defaultToWorkspaceView);
			Font = new System.Drawing.Font("Segoe UI", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));

			

			_userWorkspaceSettings = userWorkspaceSettings;
			_userProjectSettings = userProjectSettings;
			_uniqueIdToCategory = uniqueIdToCategory;
			_currentPreset = roleName;
			_defaultPreset = defaultPreset;
			_forceDefaultPreset = forceDefaultPreset;
			_availablePresets = roles;
			_perforceSection = perforceSection;
			_baseContentPaths = baseContentPaths;

			GlobalFilter = globalFilter;
			WorkspaceFilter = workspaceFilter;

			Dictionary<Guid, bool> syncCategories = WorkspaceSyncCategory.GetDefault(_uniqueIdToCategory.Values);

			GlobalControl.SyncAllProjects.Checked = GlobalFilter.AllProjects ?? false;
			GlobalControl.SyncLocalProjects.Checked = GlobalFilter.LocalProjects ?? false;
			GlobalControl.IncludeAllProjectsInSolution.Checked = GlobalFilter.AllProjectsInSln ?? false;
			GlobalControl.GenerateUprojectSpecificSolution.Checked = GlobalFilter.UprojectSpecificSln ?? false;

			_availablePresets.TryGetValue(_currentPreset, out Preset? role);
			List<string> roleViews = role != null ? role.Views.ToList() : new List<string>();

			WorkspaceSyncCategory.ApplyDelta(syncCategories, GlobalFilter.GetCategories());
			GlobalControl.SetView(GlobalFilter.View.ToArray());
			SetExcludedCategories(GlobalControl.CategoriesCheckList, _uniqueIdToCategory, null, syncCategories);

			WorkspaceSyncCategory.ApplyDelta(syncCategories, WorkspaceFilter.GetCategories());
			WorkspaceControl.SetView(_currentPreset, roleViews.ToArray(), WorkspaceFilter.View.ToArray());
			SetExcludedCategories(WorkspaceControl.CategoriesCheckList, _uniqueIdToCategory, role, syncCategories);

			SetPresetData(WorkspaceControl.PresetComboBox, _currentPreset, _availablePresets.Keys);

			if (_forceDefaultPreset)
			{
				WorkspaceControl.PresetComboBox.Enabled = false;
			}
			else
			{
				WorkspaceControl.PresetComboBox.Enabled = true;
				WorkspaceControl.PresetComboBox.SelectedValueChanged += PresetComboBox_SelectedValueChanged;
			}
			
			WorkspaceControl.CategoriesCheckList.ItemCheck += WorkspaceControl_CategoriesCheckList_ItemCheck;

			WorkspaceControl.SyncAllProjects.Checked = WorkspaceFilter.AllProjects ?? GlobalFilter.AllProjects ?? false;
			WorkspaceControl.SyncLocalProjects.Checked = WorkspaceFilter.LocalProjects ?? GlobalFilter.LocalProjects ?? false;
			WorkspaceControl.IncludeAllProjectsInSolution.Checked = WorkspaceFilter.AllProjectsInSln ?? GlobalFilter.AllProjectsInSln ?? false;
			WorkspaceControl.GenerateUprojectSpecificSolution.Checked = WorkspaceFilter.UprojectSpecificSln ?? GlobalFilter.UprojectSpecificSln ?? false;

			GlobalControl.CategoriesCheckList.ItemCheck += GlobalControl_CategoriesCheckList_ItemCheck;
			GlobalControl.SyncAllProjects.CheckStateChanged += GlobalControl_SyncAllProjects_CheckStateChanged;
			GlobalControl.SyncLocalProjects.CheckStateChanged += GlobalControl_SyncLocalProjects_CheckStateChanged;
			GlobalControl.IncludeAllProjectsInSolution.CheckStateChanged += GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged;
			GlobalControl.GenerateUprojectSpecificSolution.CheckStateChanged += GlobalControl_GenerateUprojectSpecificSolution_CheckStateChanged;

			WorkspaceControl.SyncBaseContentCheckBox.Size = new System.Drawing.Size(0,0);
			WorkspaceControl.SyncBaseContentCheckBox.AutoCheck = false;
			WorkspaceControl.SyncBaseContentCheckBox.Visible = true;

			WorkspaceControl.ViewDataGrid.ReadOnly = false;

			if (_userWorkspaceSettings.ProjectPath.EndsWith(".uefnproject", StringComparison.OrdinalIgnoreCase))
			{
				WorkspaceControl.SyncBaseContentCheckBox.Checked = _userWorkspaceSettings.SyncBaseContent;
				SetReadonlyFlag(WorkspaceControl.CategoriesCheckList, !_userWorkspaceSettings.SyncBaseContent);

				if (!_userWorkspaceSettings.SyncBaseContent)
				{
					WorkspaceControl.ViewDataGrid.ReadOnly = true;
				}

				if (baseContentPaths.Any())
				{
					WorkspaceControl.SyncBaseContentCheckBox.CheckStateChanged += SyncBaseContentCheckBox_CheckStateChanged;

					string baseContentPathsStr = String.Join('\n', baseContentPaths);

					if (_userWorkspaceSettings.SyncBaseContent)
					{
						WorkspaceControl.SyncBaseContentCheckBox.AutoCheck = false;
						WorkspaceControl.SyncBaseContentToolTipTitle = "Sync Base Content";
						WorkspaceControl.SyncBaseContentToolTipContent =
							$"The following paths are synced.\n{baseContentPathsStr}";
					}
					else
					{
						WorkspaceControl.SyncBaseContentCheckBox.AutoCheck = true;
						WorkspaceControl.SyncBaseContentToolTipTitle = "Sync Base Content";
						WorkspaceControl.SyncBaseContentToolTipContent =
							"Allow wider content such as Fortnite assets to be synced rather than just UEFN project content.\n"+
							"Warning: Once enabled this cannot be unchecked, meaning you will need to create a new workspace to return to only syncing UEFN project content.\n" + 
							$"The following paths will be synced.\n{baseContentPathsStr}";
					}
				}
				else
				{
					WorkspaceControl.SyncBaseContentCheckBox.AutoCheck = false;
					WorkspaceControl.SyncBaseContentToolTipTitle = "Sync Base Content";
					WorkspaceControl.SyncBaseContentToolTipContent =
						$"There is not path in the configuration, Sync Base Content is disabled.";
				}
			}
			else
			{
				WorkspaceControl.SyncBaseContentToolTipTitle = "Sync Base Content";
				WorkspaceControl.SyncBaseContentToolTipContent = "Sync Base Content is only relevant for FNE projects.";
				WorkspaceControl.SyncBaseContentCheckBox.AutoCheck = false;
				WorkspaceControl.SyncBaseContentCheckBox.Visible = false;
			}
		}

		private void SyncBaseContentCheckBox_CheckStateChanged(object? sender, EventArgs e)
		{
			// add popup here to confirm enabling it
			if (WorkspaceControl.SyncBaseContentCheckBox.Checked)
			{
				using SyncBaseContentConfirmationWindow form = new SyncBaseContentConfirmationWindow(String.Join('\n', _baseContentPaths));

				if (form.ShowDialog() == DialogResult.Cancel)
				{
					WorkspaceControl.SyncBaseContentCheckBox.Checked = false;
					return;
				}
			}

			bool isReadOnly = !WorkspaceControl.SyncBaseContentCheckBox.Checked;
			SetReadonlyFlag(WorkspaceControl.CategoriesCheckList, !WorkspaceControl.SyncBaseContentCheckBox.Checked);

			WorkspaceControl.ViewDataGrid.ReadOnly = isReadOnly;

			if (isReadOnly)
			{
				if (WorkspaceControl.PresetComboBox.SelectedItem is PresetItem item)
				{
					string preset = item.Name;
					_availablePresets.TryGetValue(preset, out Preset? role);

					WorkspaceControl.CategoriesCheckList.ItemCheck -= WorkspaceControl_CategoriesCheckList_ItemCheck;

					Dictionary<Guid, bool> syncCategories = WorkspaceSyncCategory.GetDefault(_uniqueIdToCategory.Values);
					WorkspaceSyncCategory.ApplyDelta(syncCategories, GlobalFilter.GetCategories());
					WorkspaceSyncCategory.ApplyDelta(syncCategories, WorkspaceFilter.GetCategories());
					SetExcludedCategories(WorkspaceControl.CategoriesCheckList, _uniqueIdToCategory, role, syncCategories);
					// update the readonly flag
					SetReadonlyFlag(WorkspaceControl.CategoriesCheckList, !WorkspaceControl.SyncBaseContentCheckBox.Checked);

					WorkspaceControl.CategoriesCheckList.ItemCheck += WorkspaceControl_CategoriesCheckList_ItemCheck;
				}
			}
		}

		private void PresetComboBox_SelectedValueChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.CategoriesCheckList.ItemCheck -= WorkspaceControl_CategoriesCheckList_ItemCheck;
			GlobalControl.CategoriesCheckList.ItemCheck -= GlobalControl_CategoriesCheckList_ItemCheck;

			if (WorkspaceControl.PresetComboBox.SelectedItem is PresetItem item)
			{
				string preset = item.Name;

				_availablePresets.TryGetValue(preset, out Preset? role);
				List<string> roleViews = role != null ? role.Views.ToList() : new List<string>();

				Dictionary<Guid, bool> syncCategories = WorkspaceSyncCategory.GetDefault(_uniqueIdToCategory.Values);

				WorkspaceControl.SetView(preset, roleViews.ToArray(), WorkspaceFilter.View.ToArray());
				WorkspaceSyncCategory.ApplyDelta(syncCategories, WorkspaceFilter.GetCategories());
				SetExcludedCategories(WorkspaceControl.CategoriesCheckList, _uniqueIdToCategory, role, syncCategories);

				// update the readonly flag
				SetReadonlyFlag(WorkspaceControl.CategoriesCheckList, !WorkspaceControl.SyncBaseContentCheckBox.Checked);
			}

			WorkspaceControl.CategoriesCheckList.ItemCheck += WorkspaceControl_CategoriesCheckList_ItemCheck;
			GlobalControl.CategoriesCheckList.ItemCheck += GlobalControl_CategoriesCheckList_ItemCheck;
		}

		private void GlobalControl_CategoriesCheckList_ItemCheck(object? sender, ItemCheckEventArgs e)
		{
			// do not allow changing the value of locked categories
			if (sender is not System.Windows.Forms.CheckedListBox listBox)
			{
				return;
			}

			if (listBox.Items[e.Index] is not CheckListItem item)
			{
				return;
			}

			if (item.Locked)
			{
				e.NewValue = e.CurrentValue;
				return;
			}

			// only report to the workspace the values that are not locked
			if (WorkspaceControl.CategoriesCheckList.Items[e.Index] is not CheckListItem workspaceItem || workspaceItem.Locked || workspaceItem.IsReadOnly)
			{
				return;
			}

			if (item.UniqueId.Equals(workspaceItem.UniqueId))
			{
				WorkspaceControl.CategoriesCheckList.SetItemCheckState(e.Index, e.NewValue);
			}
		}

		private void WorkspaceControl_CategoriesCheckList_ItemCheck(object? sender, ItemCheckEventArgs e)
		{
			// do not allow changing the value of locked categories
			if (sender is System.Windows.Forms.CheckedListBox listBox)
			{
				if (listBox.Items[e.Index] is CheckListItem item && (item.Locked || item.IsReadOnly))
				{
					e.NewValue = e.CurrentValue;
					return;
				}
			}
		}

		private void GlobalControl_SyncAllProjects_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.SyncAllProjects.Checked = GlobalControl.SyncAllProjects.Checked;
		}

		private void GlobalControl_SyncLocalProjects_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.SyncLocalProjects.Checked = GlobalControl.SyncLocalProjects.Checked;
		}

		private void GlobalControl_IncludeAllProjectsInSolution_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.IncludeAllProjectsInSolution.Checked = GlobalControl.IncludeAllProjectsInSolution.Checked;
		}

		private void GlobalControl_GenerateUprojectSpecificSolution_CheckStateChanged(object? sender, EventArgs e)
		{
			WorkspaceControl.GenerateUprojectSpecificSolution.Checked = GlobalControl.GenerateUprojectSpecificSolution.Checked;
		}

		private void SetPresetData(ComboBox presetComboBox, string currentPreset, IEnumerable<string> availablePresets)
		{
			presetComboBox.BeginUpdate();
			presetComboBox.Items.Clear();
			
			foreach (string presetName in availablePresets)
			{
				string suffix = String.Empty;
				if (!String.IsNullOrWhiteSpace(_defaultPreset) && presetName.Equals(_defaultPreset, StringComparison.OrdinalIgnoreCase))
				{
					suffix = "(Default)";
				}

				PresetItem item = new PresetItem()
				{
					Name = presetName, 
					Suffix = suffix
				};
				presetComboBox.Items.Add(item);

				if (presetName.Equals(currentPreset, StringComparison.OrdinalIgnoreCase))
				{
					presetComboBox.SelectedItem = item;
				}
			}
			
			presetComboBox.EndUpdate();
		}

		private static void SetExcludedCategories(
			CheckedListBox listBox, 
			Dictionary<Guid, WorkspaceSyncCategory> uniqueIdToFilter,
			Preset? role,
			Dictionary<Guid, bool> categoryIdToSetting)
		{
			listBox.BeginUpdate();
			listBox.Items.Clear();
			
			foreach (WorkspaceSyncCategory filter in uniqueIdToFilter.Values)
			{
				if (filter.Hidden)
				{
					continue;
				}

				CheckState state = CheckState.Checked;
				if (!categoryIdToSetting[filter.UniqueId])
				{
					state = CheckState.Unchecked;
				}
				
				CheckListItem item = new CheckListItem()
				{
					UniqueId = filter.UniqueId,
					CategoryName = filter.Name,
					Locked = false,
					Preset = String.Empty,
					Enabled = filter.Enable,
					Paths = filter.Paths,
					ToolTip = String.Join('\n', filter.Paths)
				};

				if (role != null && role.Categories.TryGetValue(filter.UniqueId, out RoleCategory? category))
				{
					item.Preset = role.Name;
					item.Locked = true;
					item.Enabled = category.Enabled;
					state = category.Enabled ? CheckState.Checked : CheckState.Unchecked;
				}
					
				listBox.Items.Add(item, state);
			}

			listBox.EndUpdate();
		}

		private void SetReadonlyFlag(CheckedListBox list, bool isReadOnly)
		{
			if (!_userWorkspaceSettings.ProjectPath.EndsWith(".uefnproject", StringComparison.OrdinalIgnoreCase))
			{
				return;
			}

			foreach (CheckListItem item in list.Items.OfType<CheckListItem>())
			{
				item.IsReadOnly = isReadOnly;
			}
		}

		private void GetSettings(out FilterSettings newGlobalFilter, out FilterSettings newWorkspaceFilter)
		{
			Dictionary<Guid, bool> defaultSyncCategories = WorkspaceSyncCategory.GetDefault(_uniqueIdToCategory.Values);

			newGlobalFilter = new FilterSettings();
			newGlobalFilter.View.AddRange(GlobalControl.GetView());
			newGlobalFilter.AllProjects = GlobalControl.SyncAllProjects.Checked;
			newGlobalFilter.LocalProjects = GlobalControl.SyncLocalProjects.Checked;
			newGlobalFilter.AllProjectsInSln = GlobalControl.IncludeAllProjectsInSolution.Checked;
			newGlobalFilter.UprojectSpecificSln = GlobalControl.GenerateUprojectSpecificSolution.Checked;

			Dictionary<Guid, bool> globalSyncCategories = GetCategorySettings(GlobalControl.CategoriesCheckList, GlobalFilter.GetCategories(), includeLockedItems: false);
			newGlobalFilter.SetCategories(WorkspaceSyncCategory.GetDelta(defaultSyncCategories, globalSyncCategories));

			newWorkspaceFilter = new FilterSettings();
			newWorkspaceFilter.View.AddRange(WorkspaceControl.GetView());
			newWorkspaceFilter.AllProjects = (WorkspaceControl.SyncAllProjects.Checked == newGlobalFilter.AllProjects) ? (bool?)null : WorkspaceControl.SyncAllProjects.Checked;
			newWorkspaceFilter.LocalProjects = (WorkspaceControl.SyncLocalProjects.Checked == newGlobalFilter.LocalProjects) ? (bool?)null : WorkspaceControl.SyncLocalProjects.Checked;
			newWorkspaceFilter.AllProjectsInSln = (WorkspaceControl.IncludeAllProjectsInSolution.Checked == newGlobalFilter.AllProjectsInSln) ? (bool?)null : WorkspaceControl.IncludeAllProjectsInSolution.Checked;
			newWorkspaceFilter.UprojectSpecificSln = (WorkspaceControl.GenerateUprojectSpecificSolution.Checked == newGlobalFilter.UprojectSpecificSln) ? (bool?)null : WorkspaceControl.GenerateUprojectSpecificSolution.Checked;

			Dictionary<Guid, bool> workspaceSyncCategories = GetCategorySettings(WorkspaceControl.CategoriesCheckList, WorkspaceFilter.GetCategories());
			newWorkspaceFilter.SetCategories(WorkspaceSyncCategory.GetDelta(globalSyncCategories, workspaceSyncCategories));
		}

		private Dictionary<Guid, bool> GetCategorySettings(CheckedListBox listBox, IEnumerable<KeyValuePair<Guid, bool>> originalSettings, bool includeLockedItems = true)
		{
			HashSet<Guid> locked = new HashSet<Guid>();
			Dictionary<Guid, bool> result = new Dictionary<Guid, bool>();
			for (int idx = 0; idx < listBox.Items.Count; idx++)
			{
				if (listBox.Items[idx] is CheckListItem item)
				{
					if (item.Locked && includeLockedItems == false)
					{
						locked.Add(item.UniqueId);
						continue;
					}

					Guid uniqueId = item.UniqueId;
					if (!result.ContainsKey(uniqueId))
					{
						result[uniqueId] = listBox.GetItemCheckState(idx) == CheckState.Checked;
					}	
				}
			}
			foreach (KeyValuePair<Guid, bool> originalSetting in originalSettings)
			{
				if (!_uniqueIdToCategory.ContainsKey(originalSetting.Key) || locked.Contains(originalSetting.Key))
				{
					result[originalSetting.Key] = originalSetting.Value;
				}
			}
			return result;
		}

		private void OkButton_Click(object sender, EventArgs e)
		{
			GetSettings(out FilterSettings newGlobalFilter, out FilterSettings newWorkspaceFilter);

			if (newGlobalFilter.View.Any(x => x.Contains("//", StringComparison.Ordinal)) || newWorkspaceFilter.View.Any(x => x.Contains("//", StringComparison.Ordinal)))
			{
				if (MessageBox.Show(this, "Custom views should be relative to the stream root (eg. -/Engine/...).\r\n\r\nFull depot paths (eg. //depot/...) will not match any files.\r\n\r\nAre you sure you want to continue?", "Invalid view", MessageBoxButtons.OKCancel) != System.Windows.Forms.DialogResult.OK)
				{
					return;
				}
			}

			GlobalFilter = newGlobalFilter;
			WorkspaceFilter = newWorkspaceFilter;
			
			if (WorkspaceControl.SyncBaseContentCheckBox.Checked)
			{
				_userWorkspaceSettings.SyncBaseContent = true;
			}

			if (WorkspaceControl.PresetComboBox.SelectedItem is PresetItem item)
			{
				string preset = item.Name;
				if (!_userProjectSettings.Preset.Equals(preset, StringComparison.OrdinalIgnoreCase))
				{
					_userProjectSettings.Preset = preset;
					_userProjectSettings.PresetSetByUser = true;
				}
			}

			DialogResult = DialogResult.OK;
		}

		private void CancButton_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
		}

		private void ShowCombinedView_Click(object sender, EventArgs e)
		{
			GetSettings(out FilterSettings newGlobalFilter, out FilterSettings newWorkspaceFilter);

			List<string> filter = new List<string>();

			if (WorkspaceControl.SyncBaseContentCheckBox.Checked)
			{
				filter.AddRange(_baseContentPaths);
			}

			filter.AddRange(GlobalSettingsFile.GetCombinedSyncFilter(_uniqueIdToCategory, _currentPreset, _availablePresets, newGlobalFilter, newWorkspaceFilter, _perforceSection));
			if (filter.Count == 0)
			{
				filter.Add("All files will be synced.");
			}
			
#pragma warning disable CA2000 // Dispose objects before losing scope
			CombinedViewsWindow combinedViewsWindow = new CombinedViewsWindow(filter);
#pragma warning restore CA2000

			combinedViewsWindow.FormBorderStyle = FormBorderStyle.FixedDialog;
			combinedViewsWindow.ShowDialog();
		}
	}
}
