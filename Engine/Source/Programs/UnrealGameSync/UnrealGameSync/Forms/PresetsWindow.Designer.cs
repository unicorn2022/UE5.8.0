// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealGameSync.Properties;

namespace UnrealGameSync.Forms
{
	partial class PresetsWindow
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			components = new System.ComponentModel.Container();
			System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle1 = new System.Windows.Forms.DataGridViewCellStyle();
			System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle2 = new System.Windows.Forms.DataGridViewCellStyle();
			System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(PresetsWindow));
			RoleSelector = new System.Windows.Forms.ComboBox();
			SyncFilters = new System.Windows.Forms.DataGridView();
			Category = new System.Windows.Forms.DataGridViewTextBoxColumn();
			Include = new System.Windows.Forms.DataGridViewTextBoxColumn();
			Paths = new System.Windows.Forms.DataGridViewTextBoxColumn();
			Views = new System.Windows.Forms.ListView();
			label3 = new System.Windows.Forms.Label();
			label4 = new System.Windows.Forms.Label();
			CurrentPreset = new System.Windows.Forms.Label();
			SelectPreset = new System.Windows.Forms.Button();
			CancButton = new System.Windows.Forms.Button();
			OkButton = new System.Windows.Forms.Button();
			CopyToGlobal = new System.Windows.Forms.Button();
			CopyToWorkspace = new System.Windows.Forms.Button();
			groupBox1 = new System.Windows.Forms.GroupBox();
			groupBox2 = new System.Windows.Forms.GroupBox();
			PresetComboBoxToolTip = new System.Windows.Forms.ToolTip(components);
			HeaderTableLayout = new System.Windows.Forms.TableLayoutPanel();
			TopLevelFlowLayout = new System.Windows.Forms.FlowLayoutPanel();
			FooterFlowLayout = new System.Windows.Forms.FlowLayoutPanel();
			DialogControlsFlowLayout = new System.Windows.Forms.FlowLayoutPanel();
			CopyToToolTip = new System.Windows.Forms.ToolTip(components);
			((System.ComponentModel.ISupportInitialize)SyncFilters).BeginInit();
			groupBox1.SuspendLayout();
			groupBox2.SuspendLayout();
			HeaderTableLayout.SuspendLayout();
			TopLevelFlowLayout.SuspendLayout();
			FooterFlowLayout.SuspendLayout();
			DialogControlsFlowLayout.SuspendLayout();
			SuspendLayout();
			// 
			// RoleSelector
			// 
			RoleSelector.Anchor = System.Windows.Forms.AnchorStyles.Left;
			RoleSelector.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
			RoleSelector.FormattingEnabled = true;
			RoleSelector.Location = new System.Drawing.Point(107, 31);
			RoleSelector.Margin = new System.Windows.Forms.Padding(5);
			RoleSelector.Name = "RoleSelector";
			RoleSelector.Size = new System.Drawing.Size(94, 23);
			RoleSelector.TabIndex = 0;
			// 
			// SyncFilters
			// 
			SyncFilters.AllowUserToAddRows = false;
			SyncFilters.AllowUserToDeleteRows = false;
			SyncFilters.BackgroundColor = System.Drawing.SystemColors.Window;
			SyncFilters.CausesValidation = false;
			SyncFilters.ClipboardCopyMode = System.Windows.Forms.DataGridViewClipboardCopyMode.EnableAlwaysIncludeHeaderText;
			dataGridViewCellStyle1.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
			dataGridViewCellStyle1.BackColor = System.Drawing.SystemColors.Control;
			dataGridViewCellStyle1.Font = new System.Drawing.Font("Segoe UI", 9F);
			dataGridViewCellStyle1.ForeColor = System.Drawing.SystemColors.WindowText;
			dataGridViewCellStyle1.SelectionBackColor = System.Drawing.SystemColors.Highlight;
			dataGridViewCellStyle1.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
			dataGridViewCellStyle1.WrapMode = System.Windows.Forms.DataGridViewTriState.True;
			SyncFilters.ColumnHeadersDefaultCellStyle = dataGridViewCellStyle1;
			SyncFilters.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize;
			SyncFilters.Columns.AddRange(new System.Windows.Forms.DataGridViewColumn[] { Category, Include, Paths });
			dataGridViewCellStyle2.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
			dataGridViewCellStyle2.BackColor = System.Drawing.SystemColors.Window;
			dataGridViewCellStyle2.Font = new System.Drawing.Font("Segoe UI", 9F);
			dataGridViewCellStyle2.ForeColor = System.Drawing.SystemColors.ControlText;
			dataGridViewCellStyle2.SelectionBackColor = System.Drawing.SystemColors.Highlight;
			dataGridViewCellStyle2.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
			dataGridViewCellStyle2.WrapMode = System.Windows.Forms.DataGridViewTriState.False;
			SyncFilters.DefaultCellStyle = dataGridViewCellStyle2;
			SyncFilters.Location = new System.Drawing.Point(13, 22);
			SyncFilters.Name = "SyncFilters";
			SyncFilters.ReadOnly = true;
			SyncFilters.RowHeadersVisible = false;
			SyncFilters.RowHeadersWidth = 62;
			SyncFilters.Size = new System.Drawing.Size(644, 200);
			SyncFilters.TabIndex = 1;
			SyncFilters.TabStop = false;
			// 
			// Category
			// 
			Category.HeaderText = "Category";
			Category.MinimumWidth = 128;
			Category.Name = "Category";
			Category.ReadOnly = true;
			Category.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable;
			Category.Width = 256;
			// 
			// Include
			// 
			Include.HeaderText = "Include";
			Include.MinimumWidth = 64;
			Include.Name = "Include";
			Include.ReadOnly = true;
			Include.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable;
			Include.Width = 64;
			// 
			// Paths
			// 
			Paths.HeaderText = "Paths";
			Paths.MinimumWidth = 128;
			Paths.Name = "Paths";
			Paths.ReadOnly = true;
			Paths.SortMode = System.Windows.Forms.DataGridViewColumnSortMode.NotSortable;
			Paths.Width = 320;
			// 
			// Views
			// 
			Views.CausesValidation = false;
			Views.GridLines = true;
			Views.HeaderStyle = System.Windows.Forms.ColumnHeaderStyle.None;
			Views.Location = new System.Drawing.Point(13, 22);
			Views.Name = "Views";
			Views.ShowGroups = false;
			Views.Size = new System.Drawing.Size(644, 112);
			Views.TabIndex = 4;
			Views.UseCompatibleStateImageBehavior = false;
			Views.View = System.Windows.Forms.View.List;
			// 
			// label3
			// 
			label3.Anchor = System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right;
			label3.AutoSize = true;
			label3.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			label3.ImageAlign = System.Drawing.ContentAlignment.MiddleLeft;
			label3.Location = new System.Drawing.Point(5, 5);
			label3.Margin = new System.Windows.Forms.Padding(5);
			label3.Name = "label3";
			label3.Size = new System.Drawing.Size(92, 15);
			label3.TabIndex = 5;
			label3.Text = "Current Preset:";
			label3.TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
			// 
			// label4
			// 
			label4.Anchor = System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right;
			label4.AutoSize = true;
			label4.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			label4.ImageAlign = System.Drawing.ContentAlignment.MiddleLeft;
			label4.Location = new System.Drawing.Point(5, 35);
			label4.Margin = new System.Windows.Forms.Padding(5);
			label4.Name = "label4";
			label4.Size = new System.Drawing.Size(92, 15);
			label4.TabIndex = 6;
			label4.Text = "Select Preset:";
			label4.TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
			// 
			// CurrentPreset
			// 
			CurrentPreset.Anchor = System.Windows.Forms.AnchorStyles.Left;
			CurrentPreset.AutoSize = true;
			CurrentPreset.Font = new System.Drawing.Font("Segoe UI", 9F);
			CurrentPreset.Location = new System.Drawing.Point(107, 5);
			CurrentPreset.Margin = new System.Windows.Forms.Padding(5);
			CurrentPreset.Name = "CurrentPreset";
			CurrentPreset.Size = new System.Drawing.Size(0, 15);
			CurrentPreset.TabIndex = 7;
			// 
			// SelectPreset
			// 
			SelectPreset.Anchor = System.Windows.Forms.AnchorStyles.Left;
			SelectPreset.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			SelectPreset.ImageAlign = System.Drawing.ContentAlignment.MiddleRight;
			SelectPreset.Location = new System.Drawing.Point(211, 30);
			SelectPreset.Margin = new System.Windows.Forms.Padding(5);
			SelectPreset.Name = "SelectPreset";
			SelectPreset.Size = new System.Drawing.Size(94, 25);
			SelectPreset.TabIndex = 8;
			SelectPreset.Text = "select";
			SelectPreset.UseVisualStyleBackColor = true;
			SelectPreset.Click += SelectRole_Click;
			// 
			// CancButton
			// 
			CancButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			CancButton.Location = new System.Drawing.Point(278, 0);
			CancButton.Margin = new System.Windows.Forms.Padding(3, 0, 0, 0);
			CancButton.Name = "CancButton";
			CancButton.Size = new System.Drawing.Size(87, 26);
			CancButton.TabIndex = 10;
			CancButton.Text = "Cancel";
			CancButton.UseVisualStyleBackColor = true;
			CancButton.Click += CancButton_Click;
			// 
			// OkButton
			// 
			OkButton.Anchor = System.Windows.Forms.AnchorStyles.None;
			OkButton.Location = new System.Drawing.Point(185, 0);
			OkButton.Margin = new System.Windows.Forms.Padding(3, 0, 3, 0);
			OkButton.Name = "OkButton";
			OkButton.Size = new System.Drawing.Size(87, 26);
			OkButton.TabIndex = 9;
			OkButton.Text = "Ok";
			OkButton.UseVisualStyleBackColor = true;
			OkButton.Click += OkButton_Click;
			// 
			// CopyToGlobal
			// 
			CopyToGlobal.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			CopyToGlobal.Location = new System.Drawing.Point(3, 3);
			CopyToGlobal.Name = "CopyToGlobal";
			CopyToGlobal.Size = new System.Drawing.Size(140, 23);
			CopyToGlobal.TabIndex = 11;
			CopyToGlobal.Text = "Copy to Global";
			CopyToGlobal.UseVisualStyleBackColor = true;
			CopyToGlobal.Click += CopyToGlobal_Click;
			// 
			// CopyToWorkspace
			// 
			CopyToWorkspace.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
			CopyToWorkspace.Location = new System.Drawing.Point(149, 3);
			CopyToWorkspace.Name = "CopyToWorkspace";
			CopyToWorkspace.Size = new System.Drawing.Size(140, 23);
			CopyToWorkspace.TabIndex = 12;
			CopyToWorkspace.Text = "Copy to Workspace";
			CopyToWorkspace.UseVisualStyleBackColor = true;
			CopyToWorkspace.Click += CopyToWorkspace_Click;
			// 
			// groupBox1
			// 
			groupBox1.AutoSize = true;
			groupBox1.Controls.Add(SyncFilters);
			groupBox1.Location = new System.Drawing.Point(3, 69);
			groupBox1.Name = "groupBox1";
			groupBox1.Size = new System.Drawing.Size(663, 244);
			groupBox1.TabIndex = 13;
			groupBox1.TabStop = false;
			groupBox1.Text = "Categories";
			// 
			// groupBox2
			// 
			groupBox2.AutoSize = true;
			groupBox2.Controls.Add(Views);
			groupBox2.Location = new System.Drawing.Point(3, 319);
			groupBox2.Name = "groupBox2";
			groupBox2.Size = new System.Drawing.Size(663, 156);
			groupBox2.TabIndex = 14;
			groupBox2.TabStop = false;
			groupBox2.Text = "Custom View";
			// 
			// HeaderTableLayout
			// 
			HeaderTableLayout.AutoSize = true;
			HeaderTableLayout.ColumnCount = 3;
			HeaderTableLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
			HeaderTableLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
			HeaderTableLayout.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
			HeaderTableLayout.Controls.Add(label3, 0, 0);
			HeaderTableLayout.Controls.Add(label4, 0, 1);
			HeaderTableLayout.Controls.Add(CurrentPreset, 1, 0);
			HeaderTableLayout.Controls.Add(SelectPreset, 2, 1);
			HeaderTableLayout.Controls.Add(RoleSelector, 1, 1);
			HeaderTableLayout.Dock = System.Windows.Forms.DockStyle.Fill;
			HeaderTableLayout.Location = new System.Drawing.Point(3, 3);
			HeaderTableLayout.Name = "HeaderTableLayout";
			HeaderTableLayout.RowCount = 3;
			HeaderTableLayout.RowStyles.Add(new System.Windows.Forms.RowStyle());
			HeaderTableLayout.RowStyles.Add(new System.Windows.Forms.RowStyle());
			HeaderTableLayout.RowStyles.Add(new System.Windows.Forms.RowStyle());
			HeaderTableLayout.Size = new System.Drawing.Size(310, 60);
			HeaderTableLayout.TabIndex = 15;
			// 
			// TopLevelFlowLayout
			// 
			TopLevelFlowLayout.Controls.Add(HeaderTableLayout);
			TopLevelFlowLayout.Controls.Add(groupBox1);
			TopLevelFlowLayout.Controls.Add(groupBox2);
			TopLevelFlowLayout.Controls.Add(FooterFlowLayout);
			TopLevelFlowLayout.Controls.Add(DialogControlsFlowLayout);
			TopLevelFlowLayout.Dock = System.Windows.Forms.DockStyle.Fill;
			TopLevelFlowLayout.Location = new System.Drawing.Point(0, 0);
			TopLevelFlowLayout.Name = "TopLevelFlowLayout";
			TopLevelFlowLayout.Size = new System.Drawing.Size(670, 544);
			TopLevelFlowLayout.TabIndex = 16;
			// 
			// FooterFlowLayout
			// 
			FooterFlowLayout.AutoSize = true;
			FooterFlowLayout.Controls.Add(CopyToGlobal);
			FooterFlowLayout.Controls.Add(CopyToWorkspace);
			FooterFlowLayout.Dock = System.Windows.Forms.DockStyle.Fill;
			FooterFlowLayout.Location = new System.Drawing.Point(3, 481);
			FooterFlowLayout.Name = "FooterFlowLayout";
			FooterFlowLayout.Size = new System.Drawing.Size(292, 29);
			FooterFlowLayout.TabIndex = 16;
			// 
			// DialogControlsFlowLayout
			// 
			DialogControlsFlowLayout.Controls.Add(CancButton);
			DialogControlsFlowLayout.Controls.Add(OkButton);
			DialogControlsFlowLayout.Dock = System.Windows.Forms.DockStyle.Fill;
			DialogControlsFlowLayout.FlowDirection = System.Windows.Forms.FlowDirection.RightToLeft;
			DialogControlsFlowLayout.Location = new System.Drawing.Point(301, 481);
			DialogControlsFlowLayout.Name = "DialogControlsFlowLayout";
			DialogControlsFlowLayout.Size = new System.Drawing.Size(365, 29);
			DialogControlsFlowLayout.TabIndex = 17;
			// 
			// PresetsWindow
			// 
			AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
			AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			AutoSize = true;
			ClientSize = new System.Drawing.Size(670, 544);
			Controls.Add(TopLevelFlowLayout);
			Font = new System.Drawing.Font("Segoe UI", 9F);
			Icon = (System.Drawing.Icon)resources.GetObject("$this.Icon");
			Name = "PresetsWindow";
			Text = "Presets";
			((System.ComponentModel.ISupportInitialize)SyncFilters).EndInit();
			groupBox1.ResumeLayout(false);
			groupBox2.ResumeLayout(false);
			HeaderTableLayout.ResumeLayout(false);
			HeaderTableLayout.PerformLayout();
			TopLevelFlowLayout.ResumeLayout(false);
			TopLevelFlowLayout.PerformLayout();
			FooterFlowLayout.ResumeLayout(false);
			DialogControlsFlowLayout.ResumeLayout(false);
			ResumeLayout(false);
		}

		#endregion

		private System.Windows.Forms.ComboBox RoleSelector;
		private System.Windows.Forms.DataGridView SyncFilters;
		private System.Windows.Forms.DataGridViewTextBoxColumn Category;
		private System.Windows.Forms.DataGridViewTextBoxColumn Include;
		private System.Windows.Forms.DataGridViewTextBoxColumn Paths;
		private System.Windows.Forms.ListView Views;
		private System.Windows.Forms.Label label3;
		private System.Windows.Forms.Label label4;
		private System.Windows.Forms.Label CurrentPreset;
		private System.Windows.Forms.Button SelectPreset;
		private System.Windows.Forms.Button CancButton;
		private System.Windows.Forms.Button OkButton;
		private System.Windows.Forms.Button CopyToGlobal;
		private System.Windows.Forms.Button CopyToWorkspace;
		private System.Windows.Forms.GroupBox groupBox1;
		private System.Windows.Forms.GroupBox groupBox2;
		private System.Windows.Forms.ToolTip PresetComboBoxToolTip;
		private System.Windows.Forms.TableLayoutPanel HeaderTableLayout;
		private System.Windows.Forms.FlowLayoutPanel TopLevelFlowLayout;
		private System.Windows.Forms.FlowLayoutPanel FooterFlowLayout;
		private System.Windows.Forms.ToolTip CopyToToolTip;
		private System.Windows.Forms.FlowLayoutPanel DialogControlsFlowLayout;
	}
}